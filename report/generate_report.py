#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""读取 raw JSONL，生成 Markdown + CSV 报告。"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from collections import defaultdict
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
from bench_common import summarize_ms  # noqa: E402


def load_jsonl(path: Path) -> list[dict]:
    rows: list[dict] = []
    if not path.is_file():
        return rows
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            rows.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    return rows


def group_stats(rows: list[dict], ms_key: str = "ms_total") -> dict[str, dict]:
    buckets: dict[str, list[float]] = defaultdict(list)
    for r in rows:
        if not r.get("ok"):
            continue
        case = str(r.get("case") or "")
        lang = str(r.get("lang") or "")
        key = f"{case}|{lang}"
        try:
            buckets[key].append(float(r.get(ms_key) or 0))
        except (TypeError, ValueError):
            pass
    return {k: summarize_ms(v) for k, v in buckets.items()}


def ratio(a: float, b: float) -> str:
    if b <= 0:
        return "-"
    return f"{a / b:.2f}x"


def write_markdown(out_dir: Path, stats: dict[str, dict], meta: dict) -> None:
    lines = [
        "# OpenD Python vs C++ vs Gateway 延迟对比",
        "",
        "## 环境",
        f"- 交易环境: **{meta.get('trd_env', 'SIMULATE')}**（模拟盘）",
        f"- 标的: {meta.get('symbol', 'US.AAPL')}",
        f"- K 线: {meta.get('kl_type', 'K_5M')} × {meta.get('bars', 80)} 根",
        f"- 生成时间: {meta.get('generated_at', '')}",
        "",
    ]

    def section(title: str, keys: list[tuple[str, str]]) -> None:
        lines.append(f"## {title}")
        lines.append("")
        lines.append("| 指标 | " + " | ".join(l for _, l in keys) + " |")
        lines.append("|------|" + "|".join(["------"] * len(keys)) + "|")
        for metric in ("p50", "p95", "max", "mean"):
            row = [metric]
            for k, _ in keys:
                row.append(f"{stats.get(k, {}).get(metric, 0):.1f}ms")
            lines.append("| " + " | ".join(row) + " |")
        if len(keys) == 2:
            py_k, cpp_k = keys[0][0], keys[1][0]
            p50_py = stats.get(py_k, {}).get("p50", 0)
            p50_cpp = stats.get(cpp_k, {}).get("p50", 0)
            lines.append("")
            lines.append(f"- p50 倍率 (Python/C++): {ratio(p50_py, p50_cpp)}")
        lines.append("")

    kline_warm = [
        ("kline_warm|python", "Python 直连"),
        ("kline_warm|cpp", "C++ 直连"),
    ]
    if any(k in stats for k, _ in kline_warm):
        section("K 线热路径", kline_warm)

    order_warm = [
        ("order_warm|python_direct", "Python 直连"),
        ("order_warm|cpp", "C++ 直连"),
        ("order_warm|gateway", "Gateway HTTP"),
    ]
    if any(k in stats for k, _ in order_warm):
        section("报单热路径 (place+query+cancel)", order_warm)

    lines.extend([
        "## 说明",
        "- K 线无 gateway 路径（现网 K 线为 Python SDK 直连）",
        "- Gateway 仅测 place；cancel 为 Python 直连，与 executor `-gateway` 一致",
        "- 所有报单均在 **SIMULATE** 模拟盘执行",
        "",
    ])
    (out_dir / "summary.md").write_text("\n".join(lines), encoding="utf-8")


def write_csv(out_dir: Path, stats: dict[str, dict]) -> None:
    path = out_dir / "summary.csv"
    with path.open("w", encoding="utf-8", newline="") as f:
        w = csv.writer(f)
        w.writerow(["case_lang", "n", "p50_ms", "p95_ms", "max_ms", "mean_ms"])
        for key in sorted(stats.keys()):
            s = stats[key]
            w.writerow([key, s.get("n"), f"{s.get('p50', 0):.3f}", f"{s.get('p95', 0):.3f}",
                        f"{s.get('max', 0):.3f}", f"{s.get('mean', 0):.3f}"])


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--report-dir", required=True)
    ap.add_argument("--symbol", default="US.AAPL")
    ap.add_argument("--kl-type", default="K_5M")
    ap.add_argument("--bars", type=int, default=80)
    args = ap.parse_args()

    report_dir = Path(args.report_dir)
    raw_dir = report_dir / "raw"
    all_rows: list[dict] = []
    for name in (
        "python_kline.jsonl",
        "cpp_kline.jsonl",
        "python_order_direct.jsonl",
        "python_order_gateway.jsonl",
        "cpp_order.jsonl",
    ):
        all_rows.extend(load_jsonl(raw_dir / name))

    stats = group_stats(all_rows)
    meta = {
        "trd_env": "SIMULATE",
        "symbol": args.symbol,
        "kl_type": args.kl_type,
        "bars": args.bars,
        "generated_at": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
    }
    report_dir.mkdir(parents=True, exist_ok=True)
    write_markdown(report_dir, stats, meta)
    write_csv(report_dir, stats)
    print(f"report written: {report_dir / 'summary.md'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
