#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Python 直连 OpenD：模拟盘 place + query + cancel benchmark。"""

from __future__ import annotations

import argparse
import sys
import time
import uuid
from pathlib import Path

from bench_common import append_jsonl, load_config, ms_since, setup_python_path  # noqa: E402

setup_python_path()

from order_client import OrderClient, OrderConfig, PlaceOrderParams, extract_order_id  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]


def _one_iter(client: OrderClient, cfg: dict, i: int, out: Path, *, cold: bool) -> bool:
    code = str(cfg.get("code") or "US.AAPL")
    qty = float(cfg.get("order_qty") or 1)
    price = float(cfg.get("order_price") or 0.01)
    side = str(cfg.get("order_side") or "BUY")
    otype = str(cfg.get("order_type") or "NORMAL")
    gap = float(cfg.get("order_gap_sec") or 0.05)

    t0 = time.perf_counter()
    ms_connect = 0.0
    if cold:
        ms_connect = ms_since(t0)

    t_place = time.perf_counter()
    ret, out_place = client.place(
        PlaceOrderParams(code=code, side=side, qty=qty, price=price, order_type=otype, remark="bench_cpp")
    )
    ms_place = ms_since(t_place)
    if ret != 0:
        append_jsonl(
            out,
            {
                "case": "order_cold" if cold else "order_warm",
                "lang": "python_direct",
                "iter": i,
                "ok": False,
                "error": str(out_place),
                "ms_total": round(ms_since(t0), 3),
                "trd_env": "SIMULATE",
            },
        )
        return False

    order_id = extract_order_id(out_place)
    if not order_id:
        append_jsonl(
            out,
            {
                "case": "order_cold" if cold else "order_warm",
                "lang": "python_direct",
                "iter": i,
                "ok": False,
                "error": "missing_order_id",
                "ms_place": round(ms_place, 3),
                "trd_env": "SIMULATE",
            },
        )
        return False

    time.sleep(max(0.0, gap))
    t_query = time.perf_counter()
    ret_q, _ = client.get_order(order_id=order_id, code=code)
    ms_query = ms_since(t_query)

    time.sleep(max(0.0, gap))
    t_cancel = time.perf_counter()
    ret_c, out_c = client.cancel_order(order_id)
    ms_cancel = ms_since(t_cancel)
    ok = ret_c == 0
    append_jsonl(
        out,
        {
            "case": "order_cold" if cold else "order_warm",
            "lang": "python_direct",
            "iter": i,
            "ok": ok,
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
        print(f"  cancel fail iter={i}: {out_c}", file=sys.stderr)
    return ok


def main() -> int:
    ap = argparse.ArgumentParser(description="Python direct order benchmark (SIMULATE)")
    ap.add_argument("--config", default=str(ROOT / "config" / "bench.yaml"))
    ap.add_argument("--out", required=True)
    ap.add_argument("--mode", choices=("warm", "cold", "both"), default="both")
    args = ap.parse_args()
    cfg = load_config(Path(args.config))
    if str(cfg.get("trd_env") or "SIMULATE").upper() != "SIMULATE":
        print("ERROR: trd_env must be SIMULATE", file=sys.stderr)
        return 2

    out = Path(args.out)
    if out.exists():
        out.unlink()

    order_cfg = OrderConfig(
        host=str(cfg.get("opend_host") or "127.0.0.1"),
        port=int(cfg.get("opend_port") or 11121),
        trd_env="SIMULATE",
        trd_market="US",
    )
    warm_iters = int(cfg.get("order_iters") or 30)
    cold_iters = int(cfg.get("cold_iters") or 5)
    ok_total = 0
    need_total = 0

    if args.mode in ("cold", "both"):
        for i in range(1, cold_iters + 1):
            client = OrderClient(order_cfg)
            try:
                client.unlock()
                need_total += 1
                ok_total += int(_one_iter(client, cfg, i, out, cold=True))
            finally:
                client.close()

    if args.mode in ("warm", "both"):
        client = OrderClient(order_cfg)
        try:
            t0 = time.perf_counter()
            client.unlock()
            ms_connect = ms_since(t0)
            print(f"[python order warm] connect+unlock ms={ms_connect:.1f}")
            for i in range(1, warm_iters + 1):
                need_total += 1
                ok_total += int(_one_iter(client, cfg, i, out, cold=False))
        finally:
            client.close()

    print(f"[python order direct] ok={ok_total}/{need_total}")
    return 0 if ok_total == need_total else 1


if __name__ == "__main__":
    raise SystemExit(main())
