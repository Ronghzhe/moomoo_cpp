#!/usr/bin/env python3
"""Test 5 — OpenD API 裸调用 vs 程序封装层延时对比（Python，模拟盘）。"""

from __future__ import annotations

import argparse
import csv
import statistics
import sys
import time
import uuid
from datetime import datetime
from pathlib import Path
from typing import Any, Callable

ROOT = Path(__file__).resolve().parents[1]
BENCH_DIR = Path(__file__).resolve().parent
CSV_PATH = ROOT / "strategy/task_system/bench_logs/api_wrapper_latency.csv"
REPORT_DIR = ROOT / "reports"
CFG_PATH = ROOT / "config/bench.yaml"

for p in (str(BENCH_DIR.parent / "python"),):
    if p not in sys.path:
        sys.path.insert(0, p)

from bench_common import load_config, percentile, setup_python_path  # noqa: E402

setup_python_path()

import moomoo as ft  # noqa: E402
from order_client import OrderClient, OrderConfig, PlaceOrderParams, extract_order_id  # noqa: E402


def _ms_since(t0: float) -> float:
    return (time.perf_counter() - t0) * 1000.0


def _summarize(vals: list[float]) -> dict[str, float | int]:
    if not vals:
        return {"n": 0, "p50": 0.0, "p99": 0.0, "mean": 0.0, "stdev": 0.0}
    return {
        "n": len(vals),
        "p50": round(percentile(vals, 50), 3),
        "p99": round(percentile(vals, 99), 3),
        "mean": round(statistics.fmean(vals), 3),
        "stdev": round(statistics.pstdev(vals), 3) if len(vals) > 1 else 0.0,
    }


class BenchSession:
    def __init__(self, cfg: dict[str, Any]):
        self.cfg = cfg
        self.host = str(cfg.get("opend_host") or "127.0.0.1")
        self.port = int(cfg.get("opend_port") or 11121)
        self.code = str(cfg.get("code") or "US.AAPL")
        self.qty = float(cfg.get("order_qty") or 1)
        self.price = float(cfg.get("order_price") or 0.01)
        self.side = str(cfg.get("order_side") or "BUY")
        self.gap = float(cfg.get("order_gap_sec") or 0.05)
        ocfg = OrderConfig(host=self.host, port=self.port, trd_env=ft.TrdEnv.SIMULATE)
        self.client = OrderClient(ocfg)
        self.acc = self.client.raw
        self.trd = self.acc._ensure_ctx()

    def close(self) -> None:
        self.client.close()


def _timed(fn: Callable[[], Any]) -> tuple[float, bool, str]:
    t0 = time.perf_counter()
    try:
        fn()
        return _ms_since(t0), True, ""
    except Exception as e:
        return _ms_since(t0), False, str(e)


def _is_freq_limit(msg: str) -> bool:
    s = (msg or "").lower()
    return "high frequency" in s or "maximum 15" in s or "频控" in (msg or "") or "频率" in (msg or "")


def _sleep_gap(sess: BenchSession, *, extra: float = 0.0) -> None:
    time.sleep(max(sess.gap, 0.0) + max(extra, 0.0))


def _place_wrapped(sess: BenchSession, remark: str) -> tuple[Any, Any]:
    """Place via OrderClient; retry on OpenD 15/30s frequency limit."""
    last: Any = "place_failed"
    for attempt in range(1, 8):
        ret, out = sess.client.place(
            PlaceOrderParams(
                code=sess.code,
                side=sess.side,
                qty=sess.qty,
                price=sess.price,
                order_type="NORMAL",
                remark=remark if attempt == 1 else f"{remark}_r{attempt}",
            )
        )
        if ret == ft.RET_OK:
            return ret, out
        last = out
        if _is_freq_limit(str(out)):
            wait = max(2.2, sess.gap) * attempt
            print(f"  place freq-limit, sleep {wait:.1f}s (attempt {attempt})", flush=True)
            time.sleep(wait)
            continue
        return ret, out
    return ft.RET_ERROR, last


def _place_raw(sess: BenchSession, remark: str) -> tuple[Any, Any]:
    last: Any = "place_failed"
    side = ft.TrdSide.BUY if sess.side.upper() == "BUY" else ft.TrdSide.SELL
    for attempt in range(1, 8):
        ret, out = sess.trd.place_order(
            sess.price,
            sess.qty,
            sess.code,
            side,
            order_type=ft.OrderType.NORMAL,
            trd_env=ft.TrdEnv.SIMULATE,
            remark=remark if attempt == 1 else f"{remark}_r{attempt}",
        )
        if ret == ft.RET_OK:
            return ret, out
        last = out
        if _is_freq_limit(str(out)):
            wait = max(2.2, sess.gap) * attempt
            print(f"  place freq-limit, sleep {wait:.1f}s (attempt {attempt})", flush=True)
            time.sleep(wait)
            continue
        return ret, out
    return ft.RET_ERROR, last


def _safe_cancel(sess: BenchSession, oid: str | None) -> None:
    if not oid:
        return
    try:
        sess.client.cancel_order(oid)
    except Exception:
        pass
    _sleep_gap(sess)


def bench_place(sess: BenchSession, *, iters: int) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for i in range(1, iters + 1):
        remark_raw = f"bench5_raw_{uuid.uuid4().hex[:8]}"
        remark_wrap = f"bench5_wrap_{uuid.uuid4().hex[:8]}"
        order_ids: list[str] = []
        side = ft.TrdSide.BUY if sess.side.upper() == "BUY" else ft.TrdSide.SELL

        def raw_place() -> None:
            ret, out = sess.trd.place_order(
                sess.price,
                sess.qty,
                sess.code,
                side,
                order_type=ft.OrderType.NORMAL,
                trd_env=ft.TrdEnv.SIMULATE,
                remark=remark_raw,
            )
            if ret != ft.RET_OK:
                raise RuntimeError(str(out))
            oid = extract_order_id(out)
            if oid:
                order_ids.append(oid)

        def wrapped_place() -> None:
            ret, out = sess.client.place(
                PlaceOrderParams(
                    code=sess.code,
                    side=sess.side,
                    qty=sess.qty,
                    price=sess.price,
                    order_type="NORMAL",
                    remark=remark_wrap,
                )
            )
            if ret != ft.RET_OK:
                raise RuntimeError(str(out))
            oid = extract_order_id(out)
            if oid:
                order_ids.append(oid)

        ms_raw, ok_raw, err_raw = _timed(raw_place)
        _sleep_gap(sess)
        ms_wrap, ok_wrap, err_wrap = _timed(wrapped_place)
        overhead = ms_wrap - ms_raw if ok_raw and ok_wrap else None
        rows.append(
            {
                "api": "place",
                "iter": i,
                "raw_ms": round(ms_raw, 3),
                "wrapped_ms": round(ms_wrap, 3),
                "overhead_ms": round(overhead, 3) if overhead is not None else "",
                "ok_raw": ok_raw,
                "ok_wrapped": ok_wrap,
                "error_raw": err_raw,
                "error_wrapped": err_wrap,
            }
        )
        for oid in order_ids:
            _safe_cancel(sess, oid)
    return rows


def bench_query_cancel(sess: BenchSession, *, iters: int) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for i in range(1, iters + 1):
        print(f"query/cancel iter {i}/{iters}", flush=True)
        # --- query: 同一订单上对比 raw vs wrapped 读操作 ---
        ret, out = _place_wrapped(sess, f"bench5_q_{uuid.uuid4().hex[:8]}")
        if ret != ft.RET_OK:
            rows.append(
                {
                    "api": "query",
                    "iter": i,
                    "raw_ms": "",
                    "wrapped_ms": "",
                    "overhead_ms": "",
                    "ok_raw": False,
                    "ok_wrapped": False,
                    "error_raw": "setup_place_failed",
                    "error_wrapped": str(out),
                }
            )
            _sleep_gap(sess, extra=2.0)
            continue
        q_order_id = extract_order_id(out)
        if not q_order_id:
            rows.append(
                {
                    "api": "query",
                    "iter": i,
                    "raw_ms": "",
                    "wrapped_ms": "",
                    "overhead_ms": "",
                    "ok_raw": False,
                    "ok_wrapped": False,
                    "error_raw": "missing_order_id",
                    "error_wrapped": "",
                }
            )
            continue

        _sleep_gap(sess)

        def raw_query() -> None:
            ret, data = sess.trd.order_list_query(order_id=q_order_id, code=sess.code, trd_env=ft.TrdEnv.SIMULATE)
            if ret != ft.RET_OK:
                raise RuntimeError(str(data))

        def wrapped_query() -> None:
            ret, data = sess.client.get_order(order_id=q_order_id, code=sess.code)
            if ret != ft.RET_OK:
                raise RuntimeError(str(data))

        ms_raw_q, ok_raw_q, err_raw_q = _timed(raw_query)
        _sleep_gap(sess)
        ms_wrap_q, ok_wrap_q, err_wrap_q = _timed(wrapped_query)
        overhead_q = ms_wrap_q - ms_raw_q if ok_raw_q and ok_wrap_q else None
        rows.append(
            {
                "api": "query",
                "iter": i,
                "raw_ms": round(ms_raw_q, 3),
                "wrapped_ms": round(ms_wrap_q, 3),
                "overhead_ms": round(overhead_q, 3) if overhead_q is not None else "",
                "ok_raw": ok_raw_q,
                "ok_wrapped": ok_wrap_q,
                "error_raw": err_raw_q,
                "error_wrapped": err_wrap_q,
            }
        )
        _safe_cancel(sess, q_order_id)

        # --- cancel: 两笔独立订单，分别测 raw / wrapped 撤单（串行下单，降低频控） ---
        ret_r, out_r = _place_raw(sess, f"bench5_cr_{uuid.uuid4().hex[:8]}")
        _sleep_gap(sess)
        ret_w, out_w = _place_wrapped(sess, f"bench5_cw_{uuid.uuid4().hex[:8]}")
        raw_oid = extract_order_id(out_r) if ret_r == ft.RET_OK else None
        wrap_oid = extract_order_id(out_w) if ret_w == ft.RET_OK else None
        if not raw_oid or not wrap_oid:
            rows.append(
                {
                    "api": "cancel",
                    "iter": i,
                    "raw_ms": "",
                    "wrapped_ms": "",
                    "overhead_ms": "",
                    "ok_raw": False,
                    "ok_wrapped": False,
                    "error_raw": f"setup_place_failed:{out_r}",
                    "error_wrapped": f"setup_place_failed:{out_w}",
                }
            )
            _safe_cancel(sess, raw_oid)
            _safe_cancel(sess, wrap_oid)
            continue

        _sleep_gap(sess)

        def raw_cancel() -> None:
            ret, data = sess.trd.modify_order(
                ft.ModifyOrderOp.CANCEL,
                raw_oid,
                0.0,
                0.0,
                trd_env=ft.TrdEnv.SIMULATE,
            )
            if ret != ft.RET_OK:
                raise RuntimeError(str(data))

        def wrapped_cancel() -> None:
            ret, data = sess.client.cancel_order(wrap_oid)
            if ret != ft.RET_OK:
                raise RuntimeError(str(data))

        ms_raw_c, ok_raw_c, err_raw_c = _timed(raw_cancel)
        _sleep_gap(sess)
        ms_wrap_c, ok_wrap_c, err_wrap_c = _timed(wrapped_cancel)
        overhead_c = ms_wrap_c - ms_raw_c if ok_raw_c and ok_wrap_c else None
        rows.append(
            {
                "api": "cancel",
                "iter": i,
                "raw_ms": round(ms_raw_c, 3),
                "wrapped_ms": round(ms_wrap_c, 3),
                "overhead_ms": round(overhead_c, 3) if overhead_c is not None else "",
                "ok_raw": ok_raw_c,
                "ok_wrapped": ok_wrap_c,
                "error_raw": err_raw_c,
                "error_wrapped": err_wrap_c,
            }
        )
        _sleep_gap(sess)
    return rows


def append_csv(batch_id: str, rows: list[dict[str, Any]]) -> None:
    CSV_PATH.parent.mkdir(parents=True, exist_ok=True)
    write_header = not CSV_PATH.is_file() or CSV_PATH.stat().st_size == 0
    fields = [
        "batch_id",
        "ts",
        "api",
        "iter",
        "raw_ms",
        "wrapped_ms",
        "overhead_ms",
        "ok_raw",
        "ok_wrapped",
        "error_raw",
        "error_wrapped",
    ]
    with CSV_PATH.open("a", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        if write_header:
            w.writeheader()
        ts = datetime.now().strftime("%Y-%m-%dT%H:%M:%S")
        for r in rows:
            w.writerow({"batch_id": batch_id, "ts": ts, **r})


def write_report(batch_id: str, rows: list[dict[str, Any]]) -> Path:
    REPORT_DIR.mkdir(parents=True, exist_ok=True)
    path = REPORT_DIR / f"api_wrapper_latency_{batch_id}.md"
    lines = [
        f"# Test 5 — API 包装延时对比 ({batch_id})",
        "",
        f"**时间：** {datetime.now().strftime('%Y-%m-%d %H:%M:%S')} (UTC+8)",
        f"**环境：** OpenD SIMULATE @ 127.0.0.1:11121",
        f"**CSV：** `{CSV_PATH}`",
        "",
        "包装层 = `OrderClient` / `AccountClient`；裸调用 = `OpenSecTradeContext` SDK 直调。",
        "",
        "| api | path | n | p50_ms | p99_ms | mean_ms | stdev_ms |",
        "|-----|------|--:|-------:|-------:|--------:|---------:|",
    ]
    by_api: dict[str, dict[str, list[float]]] = {}
    for r in rows:
        api = str(r["api"])
        by_api.setdefault(api, {"raw": [], "wrapped": [], "overhead": []})
        if r.get("ok_raw") and r.get("raw_ms") not in ("", None):
            by_api[api]["raw"].append(float(r["raw_ms"]))
        if r.get("ok_wrapped") and r.get("wrapped_ms") not in ("", None):
            by_api[api]["wrapped"].append(float(r["wrapped_ms"]))
        if r.get("overhead_ms") not in ("", None):
            by_api[api]["overhead"].append(float(r["overhead_ms"]))

    for api in sorted(by_api.keys()):
        for path_name, vals in (("raw", by_api[api]["raw"]), ("wrapped", by_api[api]["wrapped"])):
            s = _summarize(vals)
            lines.append(
                f"| {api} | {path_name} | {s['n']} | {s['p50']} | {s['p99']} | {s['mean']} | {s['stdev']} |"
            )
        s_ov = _summarize(by_api[api]["overhead"])
        lines.append(
            f"| {api} | **overhead (wrap−raw)** | {s_ov['n']} | {s_ov['p50']} | {s_ov['p99']} | {s_ov['mean']} | {s_ov['stdev']} |"
        )

    lines += [
        "",
        "## 通过标准（草案）",
        "",
        "- P0: 每个 API（place/query/cancel）有 raw / wrapped 两套数据",
        "- P1: 包装开销 p50 < 1ms（place；query/cancel 同口径观测）",
        "",
    ]
    path.write_text("\n".join(lines), encoding="utf-8")
    return path


def main() -> int:
    ap = argparse.ArgumentParser(description="Test 5: raw SDK vs wrapped API latency")
    ap.add_argument("--config", default=str(CFG_PATH))
    ap.add_argument("--place-iters", type=int, default=10)
    ap.add_argument("--query-cancel-iters", type=int, default=5)
    ap.add_argument(
        "--skip-place",
        action="store_true",
        help="only run query/cancel (avoids burning place rate limit first)",
    )
    ap.add_argument(
        "--gap-sec",
        type=float,
        default=-1.0,
        help="override order_gap_sec; OpenD SIMULATE limit ~15 place/30s → prefer ≥2.2",
    )
    args = ap.parse_args()

    cfg = load_config(Path(args.config))
    if args.gap_sec >= 0:
        cfg["order_gap_sec"] = args.gap_sec
    elif float(cfg.get("order_gap_sec") or 0) < 2.0:
        # default bench.yaml is 0.05; raise for place/query/cancel to survive 15/30s limit
        cfg["order_gap_sec"] = 2.2
    batch_id = datetime.now().strftime("%Y%m%d_%H%M%S")
    sess = BenchSession(cfg)
    print(
        f"batch_id={batch_id} code={sess.code} gap={sess.gap}s "
        f"place_iters={0 if args.skip_place else args.place_iters} "
        f"query_cancel_iters={args.query_cancel_iters}",
        flush=True,
    )
    rows: list[dict[str, Any]] = []
    try:
        if not args.skip_place and args.place_iters > 0:
            rows += bench_place(sess, iters=args.place_iters)
        if args.query_cancel_iters > 0:
            rows += bench_query_cancel(sess, iters=args.query_cancel_iters)
    finally:
        sess.close()

    append_csv(batch_id, rows)
    report = write_report(batch_id, rows)
    print(f"batch_id={batch_id} rows={len(rows)} report={report}")
    for api in sorted({str(r["api"]) for r in rows}):
        sub = [r for r in rows if str(r["api"]) == api]
        raw_vals = [float(r["raw_ms"]) for r in sub if r.get("ok_raw") and r.get("raw_ms") not in ("", None)]
        wrap_vals = [float(r["wrapped_ms"]) for r in sub if r.get("ok_wrapped") and r.get("wrapped_ms") not in ("", None)]
        ov_vals = [float(r["overhead_ms"]) for r in sub if r.get("overhead_ms") not in ("", None)]
        sr, sw, so = _summarize(raw_vals), _summarize(wrap_vals), _summarize(ov_vals)
        print(f"  {api}: raw_p50={sr['p50']}ms wrapped_p50={sw['p50']}ms overhead_p50={so['p50']}ms")
    ok = all(r.get("ok_raw") and r.get("ok_wrapped") for r in rows if r.get("raw_ms") not in ("", None))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
