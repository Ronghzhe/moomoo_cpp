#!/usr/bin/env python3
"""从 task_latency.csv 生成延时统计报告（均值/中位数/方差/标准差）。

用法:
  # 压测批次（含 Worker 调度段）
  python3 bench/task_latency_analyze.py --batch 20260827_030418

  # 生产：仅 C++ 执行段，最近 7 天
  python3 bench/task_latency_analyze.py --days 7 --execution-only

  # 指定任务名（精确匹配）
  python3 bench/task_latency_analyze.py --task-name my_task_001 --execution-only
"""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "bench"))

from task_latency_stats import (  # noqa: E402
    load_csv_rows,
    summarize_scheduling_computed,
    filter_rows,
    summarize_metrics,
    write_stats_report,
)

DEFAULT_CSV = ROOT / "strategy/task_system/bench_logs/task_latency.csv"
DEFAULT_REPORT_DIR = ROOT / "reports"


def _report_label(args: argparse.Namespace) -> str:
    if args.batch:
        return args.batch
    parts: list[str] = ["prod"]
    if args.task_name:
        parts.append(args.task_name.replace("/", "_")[:48])
    if args.days:
        parts.append(f"{args.days}d")
    if not parts or parts == ["prod"]:
        parts.append(datetime.now().strftime("%Y%m%d_%H%M%S"))
    return "_".join(parts)


def main() -> int:
    ap = argparse.ArgumentParser(description="Analyze task_latency.csv with full statistics")
    ap.add_argument("--batch", default="", help="batch_id to filter (bench runs)")
    ap.add_argument("--days", type=int, default=0, help="only rows with shell_start within last N days")
    ap.add_argument("--task-name", default="", help="filter by task_name (exact match)")
    ap.add_argument(
        "--task-name-contains",
        default="",
        help="filter by task_name substring (overrides --task-name if set)",
    )
    ap.add_argument(
        "--execution-only",
        action="store_true",
        help="report Test 1 (C++ execution) only; omit Worker scheduling section",
    )
    ap.add_argument("--csv", default=str(DEFAULT_CSV))
    ap.add_argument("--out", default="", help="output md path (default reports/task_latency_stats_<label>.md)")
    ap.add_argument("--json", action="store_true", help="also print JSON summary to stdout")
    args = ap.parse_args()

    if not args.batch and args.days <= 0 and not args.task_name and not args.task_name_contains:
        ap.error("specify --batch, and/or --days, and/or --task-name / --task-name-contains")

    csv_path = Path(args.csv)
    task_filter = args.task_name_contains or args.task_name or None
    rows = load_csv_rows(
        csv_path,
        batch_id=args.batch or None,
        task_name=task_filter,
        task_name_contains=bool(args.task_name_contains),
        days=args.days if args.days > 0 else None,
    )
    label = _report_label(args)
    if not rows:
        print(f"no rows for filter label={label} in {csv_path}", file=sys.stderr)
        return 1

    out_path = Path(args.out) if args.out else DEFAULT_REPORT_DIR / f"task_latency_stats_{label}.md"
    write_stats_report(
        label,
        rows,
        csv_path=csv_path,
        report_path=out_path,
        execution_only=args.execution_only or (not args.batch),
    )

    if args.json:
        exec_metrics = [
            ("ms_shell_start_to_end", "shell"),
            ("entry_place_api_ms", "entry_api"),
            ("ms_entry_hit_to_filled", "entry_hit_to_fill"),
        ]
        payload: dict[str, object] = {
            "label": label,
            "execution_only": args.execution_only or (not args.batch),
            "entry": [
                s.as_dict()
                for s in summarize_metrics(filter_rows(rows, "entry"), exec_metrics)
            ],
            "exit": [
                s.as_dict()
                for s in summarize_metrics(
                    filter_rows(rows, "exit"),
                    [
                        ("ms_shell_start_to_end", "shell"),
                        ("exit_place_api_ms", "exit_api"),
                        ("ms_exit_hit_to_filled", "exit_hit_to_fill"),
                    ],
                )
            ],
        }
        if args.batch and not args.execution_only:
            payload["scheduling"] = [s.as_dict() for s in summarize_scheduling_computed(rows)]
        print(json.dumps(payload, ensure_ascii=False, indent=2))

    print(f"report={out_path}")
    print(f"rows={len(rows)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
