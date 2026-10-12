#!/usr/bin/env python3
"""Generate Test 6 soak report from soak_*.csv."""

from __future__ import annotations

import argparse
import csv
import statistics
from datetime import datetime
from pathlib import Path
from zoneinfo import ZoneInfo

ROOT = Path(__file__).resolve().parents[1]
BENCH_LOGS = ROOT / "strategy/task_system/bench_logs"
REPORTS = ROOT / "reports"
TZ_BJ = ZoneInfo("Asia/Shanghai")


def _f(v: str) -> float | None:
    v = str(v or "").strip()
    if not v:
        return None
    try:
        return float(v)
    except ValueError:
        return None


def main() -> int:
    ap = argparse.ArgumentParser(description="Analyze soak CSV")
    ap.add_argument("--soak-id", required=True)
    ap.add_argument("--hours", type=float, default=4.0)
    args = ap.parse_args()

    csv_path = BENCH_LOGS / f"soak_{args.soak_id}.csv"
    if not csv_path.is_file():
        raise SystemExit(f"missing {csv_path}")

    rows: list[dict[str, str]] = []
    with csv_path.open(encoding="utf-8", newline="") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        raise SystemExit("empty soak csv")

    total_ok = sum(int(r.get("tasks_ok") or 0) for r in rows)
    total_fail = sum(int(r.get("tasks_fail") or 0) for r in rows)
    total_legs = total_ok + total_fail
    fail_rate = (100.0 * total_fail / total_legs) if total_legs else 0.0
    round_fails = sum(1 for r in rows if int(r.get("round_exit_code") or 0) != 0)
    stuck_max = max(int(r.get("stuck_f_status2") or 0) for r in rows)

    exec_p50 = [_f(r.get("exec_p50_ms", "")) for r in rows]
    exec_p50 = [x for x in exec_p50 if x is not None]
    first_hour = exec_p50[: max(1, len(exec_p50) // max(1, int(args.hours)) or 1)]
    last_hour = exec_p50[-max(1, len(exec_p50) // max(1, int(args.hours)) or 1) :]
    p50_first = statistics.median(first_hour) if first_hour else None
    p50_last = statistics.median(last_hour) if last_hour else None
    drift_pct = None
    if p50_first and p50_first > 0 and p50_last is not None:
        drift_pct = round(100.0 * (p50_last - p50_first) / p50_first, 2)

    rss_exec = [_f(r.get("executor_rss_mb", "")) for r in rows]
    rss_exec = [x for x in rss_exec if x is not None]
    rss_worker = [_f(r.get("worker_rss_mb", "")) for r in rows]
    rss_worker = [x for x in rss_worker if x is not None]

    p0 = round_fails == 0 and len(rows) >= 1
    p1 = fail_rate < 1.0
    p2 = drift_pct is None or abs(drift_pct) < 20.0
    p3 = True
    if len(rss_exec) >= 2:
        p3 = rss_exec[-1] <= rss_exec[0] * 1.15 + 5.0
    if len(rss_worker) >= 2:
        p3 = p3 and (rss_worker[-1] <= rss_worker[0] * 1.15 + 5.0)

    now = datetime.now(TZ_BJ).strftime("%Y-%m-%d %H:%M:%S")
    report = REPORTS / f"soak_{args.soak_id}.md"
    lines = [
        f"# Test 6 稳定性长跑报告 `{args.soak_id}`",
        "",
        f"**生成时间（北京）：** {now}  ",
        f"**计划时长：** {args.hours}h  ",
        f"**采样轮次：** {len(rows)}",
        "",
        "## 通过标准",
        "",
        "| 级别 | 条件 | 结果 |",
        "|------|------|------|",
        f"| P0 | 跑满计划轮次无脚本级失败 | {'PASS' if p0 else 'FAIL'} |",
        f"| P1 | 任务失败率 < 1% | {'PASS' if p1 else 'FAIL'} ({fail_rate:.2f}%) |",
        f"| P2 | 执行 p50 漂移 < 20% | {'PASS' if p2 else 'FAIL'} ({drift_pct if drift_pct is not None else 'n/a'}%) |",
        f"| P3 | RSS 无显著单调增长 | {'PASS' if p3 else 'FAIL'} |",
        "",
        "## 汇总",
        "",
        f"- legs ok/fail: **{total_ok}/{total_fail}**",
        f"- round script failures: **{round_fails}**",
        f"- max stuck f_status=2 (>30min): **{stuck_max}**",
        f"- exec p50 first hour: **{p50_first}** ms",
        f"- exec p50 last hour: **{p50_last}** ms",
        f"- executor RSS first/last: **{rss_exec[0] if rss_exec else 'n/a'}** / **{rss_exec[-1] if rss_exec else 'n/a'}** MB",
        f"- worker RSS first/last: **{rss_worker[0] if rss_worker else 'n/a'}** / **{rss_worker[-1] if rss_worker else 'n/a'}** MB",
        "",
        "## 采样明细",
        "",
        "| round | ts | batch | exit | ok | fail | exec_p50 | stuck | exec_rss | worker_rss |",
        "|------:|----|-------|-----:|---:|-----:|---------:|------:|---------:|-----------:|",
    ]
    for r in rows:
        lines.append(
            f"| {r.get('round_no','')} | {r.get('sample_ts_bj','')} | {r.get('batch_id','')} | "
            f"{r.get('round_exit_code','')} | {r.get('tasks_ok','')} | {r.get('tasks_fail','')} | "
            f"{r.get('exec_p50_ms','')} | {r.get('stuck_f_status2','')} | "
            f"{r.get('executor_rss_mb','')} | {r.get('worker_rss_mb','')} |"
        )
    lines.extend(["", f"原始 CSV: `{csv_path}`", ""])
    report.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"report={report}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
