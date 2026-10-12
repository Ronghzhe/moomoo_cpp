#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""REAL round-trip benchmark: BUY qty -> wait fill -> SELL close -> wait flat."""

from __future__ import annotations

import argparse
import hashlib
import sys
import time
from pathlib import Path
from typing import Any, Optional

from bench_common import append_jsonl, load_config, ms_since, setup_python_path
from bench_rate_limit import call_with_rate_limit_retry, is_rate_limit_detail

setup_python_path()

import moomoo as ft  # noqa: E402
from order_client import OrderClient, OrderConfig, PlaceOrderParams, extract_order_id  # noqa: E402
from tools.trade_pwd_util import resolve_trade_password  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]


def market_from_code(code: str) -> str:
    return "HK" if code.upper().startswith("HK.") else "US"


def is_filled(status: str) -> bool:
    s = str(status or "").upper()
    return "FILLED" in s or "DEALT" in s


def is_terminal_unfilled(status: str) -> bool:
    s = str(status or "").upper()
    return any(x in s for x in ("CANCEL", "FAILED", "DISABLED", "DELETED", "REJECT"))


def resolve_trade_password_md5() -> tuple[Optional[str], Optional[str]]:
    pwd = resolve_trade_password()
    if not pwd:
        return None, None
    return pwd, hashlib.md5(pwd.encode("utf-8")).hexdigest()


def fetch_snapshot(code: str, host: str, port: int) -> dict[str, float]:
    ctx = ft.OpenQuoteContext(host=host, port=port)
    try:
        ret, df = ctx.get_market_snapshot([code])
        if ret != 0 or df is None or getattr(df, "empty", True):
            raise RuntimeError(f"get_market_snapshot failed ret={ret} out={df}")
        row = df.iloc[0]
        out = {
            "last": float(row.get("last_price") or 0),
            "ask": float(row.get("ask_price") or 0),
            "bid": float(row.get("bid_price") or 0),
        }
        if out["ask"] <= 0 and out["last"] > 0:
            out["ask"] = out["last"]
        if out["bid"] <= 0 and out["last"] > 0:
            out["bid"] = out["last"]
        return out
    finally:
        ctx.close()


def pick_limit_prices(snap: dict[str, float]) -> tuple[float, float]:
    ask = snap["ask"] if snap["ask"] > 0 else snap["last"]
    bid = snap["bid"] if snap["bid"] > 0 else snap["last"]
    if ask <= 0 or bid <= 0:
        raise RuntimeError(f"invalid snapshot prices: {snap}")
    # Aggressive limit: buy at ask, sell at bid (likely fill in active session).
    return ask, bid


def wait_order_status(
    client: OrderClient,
    order_id: str,
    timeout_sec: float,
    query_gap_sec: float,
    rate_limit_retry_max: int,
    rate_limit_retry_wait_sec: float,
) -> tuple[bool, str, float]:
    t0 = time.perf_counter()
    last_status = ""
    while (time.perf_counter() - t0) < timeout_sec:
        ret, out, _ = call_with_rate_limit_retry(
            lambda: client.get_order(order_id=order_id),
            label=f"query_order:{order_id}",
            max_retries=rate_limit_retry_max,
            base_wait_sec=rate_limit_retry_wait_sec,
            pre_sleep_sec=query_gap_sec,
        )
        if ret == 0 and hasattr(out, "columns") and "order_status" in out.columns and len(out) > 0:
            last_status = str(out["order_status"].iloc[0])
            if is_filled(last_status):
                return True, last_status, ms_since(t0)
            if is_terminal_unfilled(last_status):
                return False, last_status, ms_since(t0)
        elif ret != 0 and not is_rate_limit_detail(str(out)):
            return False, f"QUERY_ERR:{out}", ms_since(t0)
    return False, last_status or "TIMEOUT", ms_since(t0)


def position_qty(client: OrderClient, code: str) -> float:
    ret, out = client.raw.get_positions(code=code)
    if ret != 0 or not hasattr(out, "columns") or out.empty:
        return 0.0
    col = "qty" if "qty" in out.columns else ("can_sell_qty" if "can_sell_qty" in out.columns else "")
    if not col:
        return 0.0
    return float(out[col].iloc[0] or 0)


def place_with_push(
    client: OrderClient,
    hub: Any,
    side: str,
    code: str,
    qty: float,
    price: float,
    remark: str,
    push_timeout_sec: float,
    order_gap_sec: float,
    rate_limit_retry_max: int,
    rate_limit_retry_wait_sec: float,
) -> dict[str, Any]:
    t_place = time.perf_counter()
    ret, place_out, attempts = call_with_rate_limit_retry(
        lambda: client.place(
            PlaceOrderParams(code=code, side=side, qty=qty, price=price, order_type="NORMAL", remark=remark)
        ),
        label=f"place:{side}:{code}",
        max_retries=rate_limit_retry_max,
        base_wait_sec=rate_limit_retry_wait_sec,
        pre_sleep_sec=order_gap_sec,
    )
    ms_place = ms_since(t_place)
    order_id = extract_order_id(place_out) if ret == 0 else None
    push_row = None
    ms_order_push = 0.0
    if order_id:
        push_row = hub.wait_update(order_id, since_version=0, timeout_sec=push_timeout_sec)
        last = hub.last_update_mono(order_id)
        if last > 0:
            ms_order_push = (last - t_place) * 1000.0
    return {
        "ret_place": ret,
        "order_id": order_id or "",
        "ms_place": round(ms_place, 3),
        "ms_order_push": round(ms_order_push, 3),
        "has_push": push_row is not None,
        "place_attempts": attempts,
        "error": "" if ret == 0 else str(place_out),
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="REAL order round-trip benchmark")
    ap.add_argument("--config", default=str(ROOT / "config" / "bench_real.yaml"))
    ap.add_argument("--out", required=True)
    ap.add_argument("--code", default="")
    ap.add_argument("--qty", type=float, default=0.0)
    ap.add_argument("--price", type=float, default=0.0, help="0=auto from snapshot")
    ap.add_argument("--lang", default="python")
    ap.add_argument("--fill-timeout-sec", type=float, default=0.0)
    ap.add_argument("--push-timeout-sec", type=float, default=5.0)
    args = ap.parse_args()

    cfg = load_config(Path(args.config))
    trd_env = str(cfg.get("trd_env") or "REAL").upper()
    if trd_env != "REAL":
        print("ERROR: bench_real_roundtrip requires trd_env REAL", file=sys.stderr)
        return 2

    host = str(cfg.get("opend_host") or "127.0.0.1")
    port = int(cfg.get("opend_port") or 11121)
    code = args.code or str(cfg.get("code") or "HK.02208")
    qty = args.qty or float(cfg.get("order_qty") or 200)
    fill_timeout = args.fill_timeout_sec or float(cfg.get("fill_timeout_sec") or 60)
    order_gap_sec = float(cfg.get("order_gap_sec") or 2.5)
    query_gap_sec = float(cfg.get("query_gap_sec") or 1.0)
    rate_limit_retry_max = int(cfg.get("rate_limit_retry_max") or 5)
    rate_limit_retry_wait_sec = float(cfg.get("rate_limit_retry_wait_sec") or 2.5)
    out = Path(args.out)
    if out.exists():
        out.unlink()

    pwd, pwd_md5 = resolve_trade_password_md5()
    if not pwd:
        print("ERROR: missing trade password (MOOMOO_TRADE_PWD or fernet env)", file=sys.stderr)
        return 2

    snap = fetch_snapshot(code, host, port)
    buy_px, sell_px = pick_limit_prices(snap)
    if args.price > 0:
        buy_px = sell_px = args.price

    client = OrderClient(
        OrderConfig(
            host=host,
            port=port,
            trd_env="REAL",
            trd_market=market_from_code(code),
        )
    )
    ok = False
    row: dict[str, Any] = {
        "case": "real_roundtrip",
        "lang": args.lang,
        "code": code,
        "qty": qty,
        "trd_env": "REAL",
        "snapshot": snap,
        "buy_price": buy_px,
        "sell_price": sell_px,
        "pwd_md5_prefix": pwd_md5[:8] if pwd_md5 else "",
        "order_gap_sec": order_gap_sec,
        "query_gap_sec": query_gap_sec,
        "rate_limit_retry_max": rate_limit_retry_max,
    }
    try:
        pos_before = position_qty(client, code)
        row["pos_before"] = pos_before

        ur, ud = client.unlock(password=pwd)
        row["unlock_ret"] = ur
        if ur != 0:
            row["error"] = f"unlock failed: {ud}"
            append_jsonl(out, row)
            print(f"[real roundtrip] unlock failed ret={ur} data={ud}", file=sys.stderr)
            return 1

        hub = client.ensure_push_hub()
        buy = place_with_push(
            client,
            hub,
            "BUY",
            code,
            qty,
            buy_px,
            "bench_real_buy",
            args.push_timeout_sec,
            order_gap_sec,
            rate_limit_retry_max,
            rate_limit_retry_wait_sec,
        )
        row.update({f"buy_{k}": v for k, v in buy.items()})
        if buy["ret_place"] != 0 or not buy["order_id"]:
            row["error"] = buy.get("error") or "buy place failed"
            append_jsonl(out, row)
            return 1

        filled, buy_status, ms_buy_fill = wait_order_status(
            client,
            buy["order_id"],
            fill_timeout,
            query_gap_sec,
            rate_limit_retry_max,
            rate_limit_retry_wait_sec,
        )
        row["buy_fill_ok"] = filled
        row["buy_order_status"] = buy_status
        row["ms_buy_fill_wait"] = round(ms_buy_fill, 3)
        if not filled:
            call_with_rate_limit_retry(
                lambda: client.cancel_order(buy["order_id"]),
                label=f"cancel:{buy['order_id']}",
                max_retries=rate_limit_retry_max,
                base_wait_sec=rate_limit_retry_wait_sec,
                pre_sleep_sec=order_gap_sec,
            )
            row["error"] = f"buy not filled: {buy_status}"
            append_jsonl(out, row)
            return 1

        time.sleep(float(cfg.get("roundtrip_gap_sec") or 2))

        pos_mid = position_qty(client, code)
        row["pos_after_buy"] = pos_mid

        sell = place_with_push(
            client,
            hub,
            "SELL",
            code,
            qty,
            sell_px,
            "bench_real_sell",
            args.push_timeout_sec,
            order_gap_sec,
            rate_limit_retry_max,
            rate_limit_retry_wait_sec,
        )
        row.update({f"sell_{k}": v for k, v in sell.items()})
        if sell["ret_place"] != 0 or not sell["order_id"]:
            row["error"] = sell.get("error") or "sell place failed"
            append_jsonl(out, row)
            return 1

        sold, sell_status, ms_sell_fill = wait_order_status(
            client,
            sell["order_id"],
            fill_timeout,
            query_gap_sec,
            rate_limit_retry_max,
            rate_limit_retry_wait_sec,
        )
        row["sell_fill_ok"] = sold
        row["sell_order_status"] = sell_status
        row["ms_sell_fill_wait"] = round(ms_sell_fill, 3)

        pos_after = position_qty(client, code)
        row["pos_after"] = pos_after
        ok = sold and abs(pos_after - pos_before) < 1e-6
        row["ok"] = ok
        row["error"] = "" if ok else f"sell not filled or position mismatch: {sell_status} pos={pos_after}"
        append_jsonl(out, row)
        print(
            f"[real roundtrip] ok={ok} buy_place={buy['ms_place']}ms buy_push={buy['ms_order_push']}ms "
            f"sell_place={sell['ms_place']}ms sell_push={sell['ms_order_push']}ms pos {pos_before}->{pos_after}"
        )
        return 0 if ok else 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
