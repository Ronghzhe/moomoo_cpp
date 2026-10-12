#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Python 直连 OpenD：K_5M benchmark（模拟盘行情）。"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

from bench_common import append_jsonl, load_config, ms_since, setup_python_path  # noqa: E402

setup_python_path()

ROOT = Path(__file__).resolve().parents[1]

import moomoo as ft  # noqa: E402

KL_MAP = {
    "K_5M": ft.KLType.K_5M,
    "K_15M": ft.KLType.K_15M,
    "K_30M": ft.KLType.K_30M,
    "K_60M": ft.KLType.K_60M,
}
SUB_MAP = {
    "K_5M": ft.SubType.K_5M,
    "K_15M": ft.SubType.K_15M,
    "K_30M": ft.SubType.K_30M,
    "K_60M": ft.SubType.K_60M,
}


def _fetch_once(ctx: ft.OpenQuoteContext, code: str, kl, bars: int) -> tuple[bool, str, int, float]:
    t0 = time.perf_counter()
    ret, data = ctx.get_cur_kline(code, bars, kl, ft.AuType.QFQ)
    ms = ms_since(t0)
    src = "cur_kline"
    if ret == ft.RET_OK and data is not None and not getattr(data, "empty", True):
        return True, src, len(data), ms
    err_cur = str(data)
    t1 = time.perf_counter()
    ret_h, data_h, _ = ctx.request_history_kline(
        code, start=None, end=None, ktype=kl, autype=ft.AuType.QFQ, max_count=bars
    )
    ms_h = ms_since(t1)
    if ret_h == ft.RET_OK and data_h is not None and not getattr(data_h, "empty", True):
        return True, "history", len(data_h), ms + ms_h
    return False, f"cur={err_cur}; history={data_h}", 0, ms + ms_h


def _run_warm(cfg: dict, out: Path) -> int:
    code = str(cfg.get("code") or "US.AAPL")
    kl_key = str(cfg.get("kl_type") or "K_5M")
    kl = KL_MAP.get(kl_key, ft.KLType.K_5M)
    sub = SUB_MAP.get(kl_key, ft.SubType.K_5M)
    bars = int(cfg.get("bars") or 80)
    iters = int(cfg.get("warm_iters") or 50)
    host = str(cfg.get("opend_host") or "127.0.0.1")
    port = int(cfg.get("opend_port") or 11121)

    t_connect = time.perf_counter()
    ctx = ft.OpenQuoteContext(host=host, port=port)
    ms_connect = ms_since(t_connect)
    try:
        ctx.subscribe([code], [sub])
        ok = 0
        for i in range(1, iters + 1):
            success, src, n, ms_fetch = _fetch_once(ctx, code, kl, bars)
            append_jsonl(
                out,
                {
                    "case": "kline_warm",
                    "lang": "python",
                    "iter": i,
                    "ok": success,
                    "ms_total": round(ms_fetch, 3),
                    "ms_connect": 0.0 if i > 1 else round(ms_connect, 3),
                    "ms_fetch": round(ms_fetch, 3),
                    "symbol": code,
                    "kl_type": kl_key,
                    "bars": bars,
                    "kline_src": src,
                    "bar_count": n,
                    "trd_env": str(cfg.get("trd_env") or "SIMULATE"),
                },
            )
            ok += int(success)
        print(f"[python kline warm] ok={ok}/{iters} connect_ms={ms_connect:.1f}")
        return 0 if ok == iters else 1
    finally:
        ctx.close()


def _run_cold(cfg: dict, out: Path) -> int:
    code = str(cfg.get("code") or "US.AAPL")
    kl_key = str(cfg.get("kl_type") or "K_5M")
    kl = KL_MAP.get(kl_key, ft.KLType.K_5M)
    sub = SUB_MAP.get(kl_key, ft.SubType.K_5M)
    bars = int(cfg.get("bars") or 80)
    iters = int(cfg.get("cold_iters") or 5)
    host = str(cfg.get("opend_host") or "127.0.0.1")
    port = int(cfg.get("opend_port") or 11121)

    ok = 0
    for i in range(1, iters + 1):
        t0 = time.perf_counter()
        ctx = ft.OpenQuoteContext(host=host, port=port)
        ms_connect = ms_since(t0)
        try:
            ctx.subscribe([code], [sub])
            success, src, n, ms_fetch = _fetch_once(ctx, code, kl, bars)
            ms_total = ms_since(t0)
            append_jsonl(
                out,
                {
                    "case": "kline_cold",
                    "lang": "python",
                    "iter": i,
                    "ok": success,
                    "ms_total": round(ms_total, 3),
                    "ms_connect": round(ms_connect, 3),
                    "ms_fetch": round(ms_fetch, 3),
                    "symbol": code,
                    "kl_type": kl_key,
                    "bars": bars,
                    "kline_src": src,
                    "bar_count": n,
                    "trd_env": str(cfg.get("trd_env") or "SIMULATE"),
                },
            )
            ok += int(success)
        finally:
            ctx.close()
    print(f"[python kline cold] ok={ok}/{iters}")
    return 0 if ok == iters else 1


def main() -> int:
    ap = argparse.ArgumentParser(description="Python K_5M kline benchmark (SIMULATE quote)")
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
    rc = 0
    if args.mode in ("warm", "both"):
        rc |= _run_warm(cfg, out)
    if args.mode in ("cold", "both"):
        rc |= _run_cold(cfg, out)
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
