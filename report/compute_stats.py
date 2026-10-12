#!/usr/bin/env python3
"""Compute benchmark stats from JSONL for consolidated report."""
from __future__ import annotations

import json
import sys
from pathlib import Path


def load_jsonl(path: Path) -> list[dict]:
    rows = []
    if not path.exists():
        return rows
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            rows.append(json.loads(line))
        except json.JSONDecodeError:
            pass
    return rows


def percentile(vals: list[float], q: float) -> float:
    s = sorted(vals)
    i = max(0, min(len(s) - 1, int(round((len(s) - 1) * q))))
    return s[i]


def summarize(vals: list[float]) -> dict | None:
    if not vals:
        return None
    return {
        "n": len(vals),
        "p50": percentile(vals, 0.5),
        "p90": percentile(vals, 0.9),
        "p95": percentile(vals, 0.95),
        "mean": sum(vals) / len(vals),
        "min": min(vals),
        "max": max(vals),
    }


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    datasets = {
        "kline_warm_py": (
            root / "reports/opend_bench_20260803_064311/raw/python_kline.jsonl",
            lambda r: r.get("case") == "kline_warm" and r.get("ok"),
            "ms_fetch",
        ),
        "kline_warm_cpp": (
            root / "reports/opend_bench_20260803_064311/raw/cpp_kline.jsonl",
            lambda r: r.get("case") == "kline_warm" and r.get("ok"),
            "ms_fetch",
        ),
        "push_py": (
            Path("/tmp/moomoo_push_bench_30/python_push.jsonl"),
            lambda r: r.get("ok"),
            "ms_first_push",
        ),
        "push_cpp": (
            Path("/tmp/moomoo_push_bench_30/cpp_push.jsonl"),
            lambda r: r.get("ok"),
            "ms_first_push",
        ),
        "order_place_py": (
            Path("/tmp/moomoo_order_speed_20260803_075508/py_order.jsonl"),
            lambda r: r.get("ok") and float(r.get("ms_order_push") or 0) > 0,
            "ms_place",
        ),
        "order_push_py": (
            Path("/tmp/moomoo_order_speed_20260803_075508/py_order.jsonl"),
            lambda r: r.get("ok") and float(r.get("ms_order_push") or 0) > 0,
            "ms_order_push",
        ),
        "order_place_cpp": (
            Path("/tmp/moomoo_order_speed_20260803_075508/cpp_order.jsonl"),
            lambda r: r.get("ok") and float(r.get("ms_order_push") or 0) > 0,
            "ms_place",
        ),
        "order_push_cpp": (
            Path("/tmp/moomoo_order_speed_20260803_075508/cpp_order.jsonl"),
            lambda r: r.get("ok") and float(r.get("ms_order_push") or 0) > 0,
            "ms_order_push",
        ),
    }
    out = {}
    for name, (path, pred, field) in datasets.items():
        rows = load_jsonl(path)
        vals = [float(r[field]) for r in rows if pred(r)]
        out[name] = summarize(vals)
        print(f"{name}: {out[name]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
