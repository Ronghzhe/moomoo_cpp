#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Python SIMULATE order benchmark with TradeOrder push latency."""

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


def main() -> int:
    ap = argparse.ArgumentParser(description="Python SIMULATE order + push benchmark")
    ap.add_argument("--config", default=str(ROOT / "config" / "bench.yaml"))
    ap.add_argument("--out", required=True)
    ap.add_argument("--code", default="")
    ap.add_argument("--qty", type=float, default=0.0)
    ap.add_argument("--price", type=float, default=0.0)
    ap.add_argument("--iters", type=int, default=1)
    ap.add_argument("--timeout-sec", type=float, default=5.0)
    args = ap.parse_args()

    cfg = load_config(Path(args.config))
    if str(cfg.get("trd_env") or "SIMULATE").upper() != "SIMULATE":
        print("ERROR: trd_env must be SIMULATE", file=sys.stderr)
        return 2

    code = args.code or str(cfg.get("code") or "HK.02208")
    qty = args.qty or float(cfg.get("order_qty") or 1)
    price = args.price or float(cfg.get("order_price") or 0.01)
    out = Path(args.out)
    if out.exists():
        out.unlink()

    client = OrderClient(
        OrderConfig(
            host=str(cfg.get("opend_host") or "127.0.0.1"),
            port=int(cfg.get("opend_port") or 11121),
            trd_env="SIMULATE",
            trd_market=market_from_code(code),
        )
    )
    try:
      client.unlock()
      hub = client.ensure_push_hub()
      ok_count = 0
      for i in range(1, args.iters + 1):
          t_place = time.perf_counter()
          ret, place_out = client.place(
              PlaceOrderParams(code=code, side="BUY", qty=qty, price=price, order_type="NORMAL", remark="bench_push")
          )
          ms_place = ms_since(t_place)
          order_id = extract_order_id(place_out) if ret == 0 else None
          push_row = None
          ms_push_from_place = 0.0
          if order_id:
              push_row = hub.wait_update(order_id, since_version=0, timeout_sec=args.timeout_sec)
              last = hub.last_update_mono(order_id)
              if last > 0:
                  ms_push_from_place = (last - t_place) * 1000.0
          ms_cancel = 0.0
          ret_cancel = -1
          if order_id:
              time.sleep(0.05)
              t_cancel = time.perf_counter()
              ret_cancel, _ = client.cancel_order(order_id)
              ms_cancel = ms_since(t_cancel)
          ok = ret == 0 and bool(order_id) and push_row is not None
          ok_count += int(ok)
          append_jsonl(
              out,
              {
                  "case": "order_push",
                  "lang": "python_direct",
                  "iter": i,
                  "ok": ok,
                  "code": code,
                  "order_id": order_id or "",
                  "ms_total": round(ms_place, 3),
                  "ms_place": round(ms_place, 3),
                  "ms_order_push": round(ms_push_from_place, 3),
                  "ms_cancel": round(ms_cancel, 3),
                  "ret_place": ret,
                  "ret_cancel": ret_cancel,
                  "has_push": push_row is not None,
                  "error": "" if ret == 0 else str(place_out),
                  "trd_env": "SIMULATE",
              },
          )
      print(f"[python order push] ok={ok_count}/{args.iters}")
      return 0 if ok_count == args.iters else 1
    finally:
      client.close()


if __name__ == "__main__":
    raise SystemExit(main())
