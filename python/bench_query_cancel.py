#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""SIMULATE GetOrderList (query) + ModifyOrder cancel benchmark."""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

from bench_common import append_jsonl, load_config, ms_since, setup_python_path

setup_python_path()

from order_client import OrderClient, OrderConfig, PlaceOrderParams, extract_order_id  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]


def market_from_code(code: str) -> str:
    return "HK" if code.upper().startswith("HK.") else "US"


def one_iter(
    client: OrderClient,
    cfg: dict,
    i: int,
    out: Path,
    *,
    ms_connect: float = 0.0,
) -> bool:
    code = str(cfg.get("code") or "US.AAPL")
    qty = float(cfg.get("order_qty") or 1)
    price = float(cfg.get("order_price") or 0.01)
    side = str(cfg.get("order_side") or "BUY")
    api_gap = float(cfg.get("api_gap_sec") or 0.5)

    t0 = time.perf_counter()
    t_place = time.perf_counter()
    ret_p, out_p = client.place(
        PlaceOrderParams(code=code, side=side, qty=qty, price=price, order_type="NORMAL", remark="bench_qc")
    )
    ms_place = ms_since(t_place)
    order_id = extract_order_id(out_p) if ret_p == 0 else None
    if not order_id:
        append_jsonl(
            out,
            {
                "case": "query_cancel_warm",
                "lang": "python",
                "iter": i,
                "ok": False,
                "ok_query": False,
                "ok_cancel": False,
                "error": "place_failed" if ret_p != 0 else "missing_order_id",
                "ms_place": round(ms_place, 3),
                "code": code,
                "trd_env": "SIMULATE",
            },
        )
        return False

    time.sleep(max(0.0, api_gap))
    t_query = time.perf_counter()
    ret_q, out_q = client.get_order(order_id=order_id, code=code)
    ms_query = ms_since(t_query)
    ok_query = ret_q == 0

    time.sleep(max(0.0, api_gap))
    t_cancel = time.perf_counter()
    ret_c, out_c = client.cancel_order(order_id)
    ms_cancel = ms_since(t_cancel)
    ok_cancel = ret_c == 0

    ok = ok_query and ok_cancel
    append_jsonl(
        out,
        {
            "case": "query_cancel_warm",
            "lang": "python",
            "iter": i,
            "ok": ok,
            "ok_query": ok_query,
            "ok_cancel": ok_cancel,
            "order_id": order_id,
            "ms_total": round(ms_since(t0), 3),
            "ms_connect": round(ms_connect, 3),
            "ms_place": round(ms_place, 3),
            "ms_query": round(ms_query, 3),
            "ms_cancel": round(ms_cancel, 3),
            "ret_query": ret_q,
            "ret_cancel": ret_c,
            "code": code,
            "trd_env": "SIMULATE",
        },
    )
    if not ok:
        print(f"  iter={i} query={ret_q} cancel={ret_c} cancel_out={out_c}", file=sys.stderr)
    return ok


def main() -> int:
    ap = argparse.ArgumentParser(description="Python SIMULATE query+cancel benchmark")
    ap.add_argument("--config", default=str(ROOT / "config" / "bench_query_cancel.yaml"))
    ap.add_argument("--out", required=True)
    ap.add_argument("--iters", type=int, default=0)
    args = ap.parse_args()

    cfg = load_config(Path(args.config))
    if str(cfg.get("trd_env") or "SIMULATE").upper() != "SIMULATE":
        print("ERROR: trd_env must be SIMULATE", file=sys.stderr)
        return 2

    warm_iters = args.iters or int(cfg.get("warm_iters") or 10)
    iter_gap = float(cfg.get("iter_gap_sec") or 3.0)
    out = Path(args.out)
    if out.exists():
        out.unlink()

    client = OrderClient(
        OrderConfig(
            host=str(cfg.get("opend_host") or "127.0.0.1"),
            port=int(cfg.get("opend_port") or 11121),
            trd_env="SIMULATE",
            trd_market=market_from_code(str(cfg.get("code") or "US.AAPL")),
        )
    )
    ok_total = 0
    try:
        t0 = time.perf_counter()
        client.unlock()
        ms_connect = ms_since(t0)
        print(f"[python query_cancel] connect+unlock ms={ms_connect:.1f}")
        for i in range(1, warm_iters + 1):
            ok_total += int(one_iter(client, cfg, i, out, ms_connect=ms_connect if i == 1 else 0.0))
            if i < warm_iters:
                time.sleep(iter_gap)
    finally:
        client.close()

    print(f"[python query_cancel] ok={ok_total}/{warm_iters}")
    return 0 if ok_total == warm_iters else 1


if __name__ == "__main__":
    raise SystemExit(main())
