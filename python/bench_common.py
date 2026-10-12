#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Benchmark 公共：配置加载、JSONL 写入、统计。"""

from __future__ import annotations

import json
import statistics
import sys
import time
from pathlib import Path
from typing import Any

try:
    import yaml  # type: ignore
except ImportError:
    yaml = None


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CFG = ROOT / "config" / "bench.yaml"
MOOMOO_PY = Path("/home/ubuntu/workspace/moomoo_python")
MOOMOO_SDK = Path("/home/ubuntu/workspace/MMAPI4Python_10.4.6408")


def setup_python_path() -> None:
    for p in (str(MOOMOO_SDK), str(MOOMOO_PY), str(MOOMOO_PY / "strategy")):
        if p not in sys.path:
            sys.path.insert(0, p)


def load_config(path: Path | None = None) -> dict[str, Any]:
    cfg_path = path or DEFAULT_CFG
    text = cfg_path.read_text(encoding="utf-8")
    if yaml is not None:
        data = yaml.safe_load(text)
        return data if isinstance(data, dict) else {}
    # 极简 fallback：只解析 trd_env / host / port 等常见键
    out: dict[str, Any] = {}
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#") or ":" not in line:
            continue
        k, v = line.split(":", 1)
        k = k.strip()
        v = v.strip().strip("'\"")
        if v.isdigit():
            out[k] = int(v)
        else:
            try:
                out[k] = float(v)
            except ValueError:
                out[k] = v
    return out


def ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def append_jsonl(path: Path, row: dict[str, Any]) -> None:
    ensure_parent(path)
    row = dict(row)
    row.setdefault("ts", time.strftime("%Y-%m-%dT%H:%M:%S"))
    with path.open("a", encoding="utf-8") as f:
        f.write(json.dumps(row, ensure_ascii=False) + "\n")


def percentile(values: list[float], pct: float) -> float:
    if not values:
        return 0.0
    if len(values) == 1:
        return values[0]
    xs = sorted(values)
    k = (len(xs) - 1) * (pct / 100.0)
    f = int(k)
    c = min(f + 1, len(xs) - 1)
    if f == c:
        return xs[f]
    return xs[f] + (xs[c] - xs[f]) * (k - f)


def summarize_ms(values: list[float]) -> dict[str, float]:
    if not values:
        return {"n": 0, "p50": 0.0, "p95": 0.0, "max": 0.0, "mean": 0.0}
    return {
        "n": len(values),
        "p50": percentile(values, 50),
        "p95": percentile(values, 95),
        "max": max(values),
        "mean": statistics.fmean(values),
    }


def ms_since(t0: float) -> float:
    return (time.perf_counter() - t0) * 1000.0
