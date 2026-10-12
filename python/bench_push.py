#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Python OpenD push benchmark: subscribe BasicQot and wait for pushes."""

from __future__ import annotations

import argparse
import threading
import time
from pathlib import Path

from bench_common import append_jsonl, load_config, ms_since, setup_python_path

setup_python_path()

ROOT = Path(__file__).resolve().parents[1]

import moomoo as ft  # noqa: E402


class BasicQuoteHandler(ft.StockQuoteHandlerBase):
    def __init__(self, t_subscribe_start: float, target_samples: int) -> None:
        super().__init__()
        self.t_subscribe_start = t_subscribe_start
        self.target_samples = target_samples
        self.first_push_ms: float | None = None
        self.count = 0
        self.event = threading.Event()

    def on_recv_rsp(self, rsp_pb):  # type: ignore[no-untyped-def]
        ret, data = super().on_recv_rsp(rsp_pb)
        now_ms = ms_since(self.t_subscribe_start)
        if ret == ft.RET_OK:
            try:
                n = len(data)
            except Exception:
                n = 1
            self.count += max(1, n)
            if self.first_push_ms is None:
                self.first_push_ms = now_ms
            if self.count >= self.target_samples:
                self.event.set()
        return ret, data


def main() -> int:
    ap = argparse.ArgumentParser(description="Python BasicQot push benchmark")
    ap.add_argument("--config", default=str(ROOT / "config" / "bench.yaml"))
    ap.add_argument("--out", required=True)
    ap.add_argument("--samples", type=int, default=1)
    ap.add_argument("--timeout-sec", type=float, default=10.0)
    args = ap.parse_args()

    cfg = load_config(Path(args.config))
    code = str(cfg.get("code") or "US.AAPL")
    host = str(cfg.get("opend_host") or "127.0.0.1")
    port = int(cfg.get("opend_port") or 11121)
    out = Path(args.out)
    if out.exists():
        out.unlink()

    t_connect = time.perf_counter()
    ctx = ft.OpenQuoteContext(host=host, port=port)
    ms_connect = ms_since(t_connect)
    try:
        t_sub = time.perf_counter()
        handler = BasicQuoteHandler(t_sub, max(1, args.samples))
        ctx.set_handler(handler)
        ret, data = ctx.subscribe([code], [ft.SubType.QUOTE], subscribe_push=True)
        ms_subscribe = ms_since(t_sub)
        ok_sub = ret == ft.RET_OK
        if ok_sub:
            handler.event.wait(args.timeout_sec)
        first_push_ms = handler.first_push_ms or 0.0
        ok = ok_sub and handler.first_push_ms is not None
        append_jsonl(
            out,
            {
                "case": "push_basicqot",
                "lang": "python",
                "iter": 1,
                "ok": ok,
                "ms_total": round(first_push_ms or ms_subscribe, 3),
                "ms_connect": round(ms_connect, 3),
                "ms_subscribe": round(ms_subscribe, 3),
                "ms_first_push": round(first_push_ms, 3),
                "push_count": handler.count,
                "symbol": code,
                "timeout_sec": args.timeout_sec,
                "error": "" if ok_sub else str(data),
            },
        )
        print(
            f"[python push] ok={ok} connect_ms={ms_connect:.1f} "
            f"subscribe_ms={ms_subscribe:.1f} first_push_ms={first_push_ms:.1f} count={handler.count}"
        )
        return 0 if ok else 1
    finally:
        ctx.close()


if __name__ == "__main__":
    raise SystemExit(main())
