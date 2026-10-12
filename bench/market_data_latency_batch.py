#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Test 3 行情延时批量：热连接主测（Python）+ 冷连接附录。"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import threading
import time
from datetime import datetime
from pathlib import Path
from typing import Any
from zoneinfo import ZoneInfo

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
from bench_common import append_jsonl, setup_python_path  # noqa: E402

setup_python_path()
import moomoo as ft  # noqa: E402

BJ = ZoneInfo("Asia/Shanghai")
ET = ZoneInfo("America/New_York")
HK = ZoneInfo("Asia/Hong_Kong")


class QuoteHandler(ft.StockQuoteHandlerBase):
    def __init__(self, t0: float, target: int) -> None:
        super().__init__()
        self.t0 = t0
        self.target = target
        self.first_push_ms: float | None = None
        self.times: list[float] = []
        self.count = 0
        self.event = threading.Event()

    def on_recv_rsp(self, rsp_pb):  # type: ignore[no-untyped-def]
        ret, data = super().on_recv_rsp(rsp_pb)
        now = (time.perf_counter() - self.t0) * 1000.0
        if ret == ft.RET_OK:
            try:
                n = len(data)
            except Exception:
                n = 1
            n = max(1, n)
            for _ in range(n):
                self.times.append(now)
            self.count += n
            if self.first_push_ms is None:
                self.first_push_ms = now
            if self.count >= self.target:
                self.event.set()
        return ret, data


def session_note() -> dict[str, str]:
    now_bj = datetime.now(BJ)
    now_et = datetime.now(ET)
    now_hk = datetime.now(HK)
    # rough session labels
    et_hm = now_et.hour * 60 + now_et.minute
    hk_hm = now_hk.hour * 60 + now_hk.minute
    if 4 * 60 <= et_hm < 9 * 60 + 30:
        us_sess = "美股盘前"
    elif 9 * 60 + 30 <= et_hm < 16 * 60:
        us_sess = "美股盘中"
    elif 16 * 60 <= et_hm < 20 * 60:
        us_sess = "美股盘后"
    else:
        us_sess = "美股休市"
    if 9 * 60 + 30 <= hk_hm < 12 * 60 or 13 * 60 <= hk_hm < 16 * 60:
        hk_sess = "港股盘中"
    elif hk_hm >= 16 * 60:
        hk_sess = "港股收盘后"
    else:
        hk_sess = "港股开盘前/午休"
    return {
        "bj": now_bj.strftime("%Y-%m-%d %H:%M:%S"),
        "et": now_et.strftime("%Y-%m-%d %H:%M:%S %Z"),
        "hk": now_hk.strftime("%Y-%m-%d %H:%M:%S %Z"),
        "us_session": us_sess,
        "hk_session": hk_sess,
    }


def load_symbols(path: Path) -> list[dict[str, Any]]:
    raw = json.loads(path.read_text(encoding="utf-8"))
    return list(raw.get("symbols") or [])


def write_symbols_txt(symbols: list[dict[str, Any]], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(s["code"] for s in symbols) + "\n", encoding="utf-8")


def run_python_warm(
    symbols: list[dict[str, Any]],
    *,
    out: Path,
    samples: int,
    timeout_sec: float,
    gap_sec: float,
    host: str,
    port: int,
) -> None:
    t_conn = time.perf_counter()
    ctx = ft.OpenQuoteContext(host=host, port=port)
    ms_connect = (time.perf_counter() - t_conn) * 1000.0
    try:
        for i, s in enumerate(symbols, 1):
            code = s["code"]
            ok = False
            ms_sub = 0.0
            first_ms = 0.0
            count = 0
            intervals: list[float] = []
            err = ""
            for attempt in range(1, 3):
                handler = QuoteHandler(0.0, samples)
                t_sub = time.perf_counter()
                handler.t0 = t_sub
                ctx.set_handler(handler)
                ret, data = ctx.subscribe([code], [ft.SubType.QUOTE], subscribe_push=True)
                ms_sub = (time.perf_counter() - t_sub) * 1000.0
                if ret != ft.RET_OK:
                    err = str(data)
                    try:
                        ctx.unsubscribe([code], [ft.SubType.QUOTE])
                    except Exception:
                        pass
                    continue
                # Phase 1: wait first push
                first_deadline = time.perf_counter() + min(15.0, timeout_sec)
                while handler.first_push_ms is None and time.perf_counter() < first_deadline:
                    handler.event.wait(0.2)
                if handler.first_push_ms is None:
                    err = "timeout_no_first_push"
                    try:
                        ctx.unsubscribe([code], [ft.SubType.QUOTE])
                    except Exception:
                        pass
                    continue
                # Phase 2: collect more pushes briefly (quiet sessions won't block full timeout)
                collect_until = time.perf_counter() + min(8.0, max(0.0, timeout_sec - 1.0))
                while handler.count < samples and time.perf_counter() < collect_until:
                    remaining = collect_until - time.perf_counter()
                    if remaining <= 0:
                        break
                    handler.event.wait(min(0.5, remaining))
                ok = True
                first_ms = float(handler.first_push_ms)
                count = handler.count
                times = handler.times
                intervals = [times[j] - times[j - 1] for j in range(1, len(times))]
                err = ""
                break
            try:
                ctx.unsubscribe([code], [ft.SubType.QUOTE])
            except Exception:
                pass
            append_jsonl(
                out,
                {
                    "case": "push_basicqot_warm",
                    "path": "warm",
                    "lang": "python",
                    "ok": ok,
                    "ms_connect": round(ms_connect, 3),
                    "ms_subscribe": round(ms_sub, 3),
                    "ms_first_push": round(first_ms, 3),
                    "push_count": count,
                    "symbol": code,
                    "name": s.get("name") or "",
                    "market": s.get("market") or "",
                    "timeout_sec": timeout_sec,
                    "target_samples": samples,
                    "intervals_ms": [round(x, 3) for x in intervals],
                    "error": err,
                },
            )
            print(
                f"[py warm] {i}/{len(symbols)} {code} ok={ok} first_push_ms={first_ms:.1f} pushes={count}",
                flush=True,
            )
            if gap_sec > 0:
                time.sleep(gap_sec)
    finally:
        ctx.close()


def run_cpp_warm(
    symbols_txt: Path,
    *,
    out: Path,
    samples: int,
    timeout_sec: float,
    gap_sec: float,
    host: str,
    port: int,
) -> int:
    bin_path = ROOT / "cpp/build/bench_push_batch"
    if not bin_path.is_file():
        print(f"ERROR missing {bin_path}", flush=True)
        return 2
    cmd = [
        str(bin_path),
        "--host",
        host,
        "--port",
        str(port),
        "--symbols-file",
        str(symbols_txt),
        "--out",
        str(out),
        "--samples",
        str(samples),
        "--timeout-sec",
        str(timeout_sec),
        "--gap-sec",
        str(gap_sec),
    ]
    print("[cpp warm] " + " ".join(cmd), flush=True)
    return subprocess.call(cmd)


def run_cold_appendix(
    symbols: list[dict[str, Any]],
    *,
    out: Path,
    rounds: int,
    timeout_sec: float,
    gap_sec: float,
    host: str,
    port: int,
) -> None:
    """Cold path: reconnect each round; C++ via bench_push, Python via new ctx."""
    cpp_bin = ROOT / "cpp/build/bench_push"
    for s in symbols:
        code = s["code"]
        for r in range(1, rounds + 1):
            # C++
            tmp = out.parent / f"cold_cpp_{code.replace('.', '_')}_r{r}.jsonl"
            if tmp.exists():
                tmp.unlink()
            if cpp_bin.is_file():
                subprocess.call(
                    [
                        str(cpp_bin),
                        "--host",
                        host,
                        "--port",
                        str(port),
                        "--symbol",
                        code,
                        "--out",
                        str(tmp),
                        "--samples",
                        "1",
                        "--timeout-sec",
                        str(timeout_sec),
                    ]
                )
                if tmp.is_file():
                    row = json.loads(tmp.read_text(encoding="utf-8").strip().splitlines()[0])
                    row.update(
                        {
                            "case": "push_basicqot_cold",
                            "path": "cold",
                            "round": r,
                            "name": s.get("name") or "",
                            "market": s.get("market") or "",
                        }
                    )
                    append_jsonl(out, row)
                    tmp.unlink(missing_ok=True)
            # Python cold
            t_conn = time.perf_counter()
            ctx = ft.OpenQuoteContext(host=host, port=port)
            ms_connect = (time.perf_counter() - t_conn) * 1000.0
            try:
                handler = QuoteHandler(0.0, 1)
                t_sub = time.perf_counter()
                handler.t0 = t_sub
                ctx.set_handler(handler)
                ret, data = ctx.subscribe([code], [ft.SubType.QUOTE], subscribe_push=True)
                ms_sub = (time.perf_counter() - t_sub) * 1000.0
                ok = False
                first_ms = 0.0
                err = ""
                if ret == ft.RET_OK:
                    handler.event.wait(timeout_sec)
                    if handler.first_push_ms is not None:
                        ok = True
                        first_ms = float(handler.first_push_ms)
                    else:
                        err = "timeout_no_first_push"
                else:
                    err = str(data)
                append_jsonl(
                    out,
                    {
                        "case": "push_basicqot_cold",
                        "path": "cold",
                        "lang": "python",
                        "ok": ok,
                        "round": r,
                        "ms_connect": round(ms_connect, 3),
                        "ms_subscribe": round(ms_sub, 3),
                        "ms_first_push": round(first_ms, 3),
                        "push_count": handler.count,
                        "symbol": code,
                        "name": s.get("name") or "",
                        "market": s.get("market") or "",
                        "timeout_sec": timeout_sec,
                        "error": err,
                    },
                )
                print(f"[py cold] {code} r{r} ok={ok} first={first_ms:.1f}", flush=True)
            finally:
                ctx.close()
            if gap_sec > 0:
                time.sleep(gap_sec)


def main() -> int:
    ap = argparse.ArgumentParser(description="Market data latency batch (warm+cold)")
    ap.add_argument(
        "--symbols",
        default=str(ROOT / "bench/config/market_data_symbols_50.json"),
    )
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=11121)
    ap.add_argument("--samples", type=int, default=20, help="pushes per warm symbol")
    ap.add_argument("--timeout-sec", type=float, default=20.0)
    ap.add_argument("--gap-sec", type=float, default=0.9)
    ap.add_argument("--warm-only", action="store_true")
    ap.add_argument("--cold-only", action="store_true")
    ap.add_argument("--cold-rounds", type=int, default=5)
    ap.add_argument("--skip-cpp", action="store_true")
    ap.add_argument("--skip-python", action="store_true")
    args = ap.parse_args()

    symbols = load_symbols(Path(args.symbols))
    batch_id = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = ROOT / "reports" / f"market_data_latency_{batch_id}"
    out_dir.mkdir(parents=True, exist_ok=True)
    meta = {
        "batch_id": batch_id,
        "session": session_note(),
        "samples": args.samples,
        "timeout_sec": args.timeout_sec,
        "gap_sec": args.gap_sec,
        "symbol_count": len(symbols),
        "note": "热连接主测+冷连接附录；BasicQot 非 OrderBook",
    }
    (out_dir / "meta.json").write_text(json.dumps(meta, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(meta, ensure_ascii=False, indent=2), flush=True)

    symbols_txt = out_dir / "symbols.txt"
    write_symbols_txt(symbols, symbols_txt)
    warm_out = out_dir / "warm.jsonl"
    cold_out = out_dir / "cold.jsonl"

    # cold subset: 5 US + 5 HK from list ends/starts for diversity
    us = [s for s in symbols if s.get("market") == "US"]
    hk = [s for s in symbols if s.get("market") == "HK"]
    cold_syms = us[:3] + us[-2:] + hk[:3] + hk[-2:]

    if not args.cold_only:
        if not args.skip_python:
            run_python_warm(
                symbols,
                out=warm_out,
                samples=args.samples,
                timeout_sec=args.timeout_sec,
                gap_sec=args.gap_sec,
                host=args.host,
                port=args.port,
            )
        if not args.skip_cpp:
            run_cpp_warm(
                symbols_txt,
                out=warm_out,
                samples=args.samples,
                timeout_sec=args.timeout_sec,
                gap_sec=args.gap_sec,
                host=args.host,
                port=args.port,
            )

    if not args.warm_only:
        run_cold_appendix(
            cold_syms,
            out=cold_out,
            rounds=args.cold_rounds,
            timeout_sec=args.timeout_sec,
            gap_sec=args.gap_sec,
            host=args.host,
            port=args.port,
        )

    print(f"DONE batch_id={batch_id} out={out_dir}", flush=True)
    print(f"NEXT: python3 bench/market_data_latency_report_zh.py --batch-dir {out_dir}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
