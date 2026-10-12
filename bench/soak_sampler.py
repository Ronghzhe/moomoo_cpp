#!/usr/bin/env python3
"""Hourly soak sampler: process RSS + stuck tasks snapshot."""

from __future__ import annotations

import argparse
import csv
import json
import sys
import time
from datetime import datetime
from pathlib import Path
from zoneinfo import ZoneInfo

ROOT = Path(__file__).resolve().parents[1]
BENCH_LOGS = ROOT / "strategy/task_system/bench_logs"
CPP_TASK = ROOT / "strategy/task_system"
PY_TASK = Path("/home/ubuntu/workspace/moomoo_python/strategy/task_system")
MYSQL_CONFIG = Path("/home/ubuntu/workspace/mysql/mysql_config.json")
TZ_BJ = ZoneInfo("Asia/Shanghai")

CSV_FIELDS = [
    "sample_ts_bj",
    "round_no",
    "batch_id",
    "round_exit_code",
    "executor_rss_mb",
    "worker_rss_mb",
    "stuck_f_status2",
    "tasks_ok",
    "tasks_fail",
    "exec_p50_ms",
    "sched_p50_ms",
]


def _rss_mb(pid: int) -> float | None:
    status = Path(f"/proc/{pid}/status")
    if not status.is_file():
        return None
    for line in status.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("VmRSS:"):
            kb = int(line.split()[1])
            return round(kb / 1024.0, 2)
    return None


def _read_pid(path: Path) -> int | None:
    if not path.is_file():
        return None
    try:
        pid = int(path.read_text(encoding="utf-8").strip())
    except ValueError:
        return None
    if pid <= 0:
        return None
    return pid if Path(f"/proc/{pid}").exists() else None


def _stuck_count(*, task_type: int = 4, min_age_sec: int = 1800) -> int:
    for p in (PY_TASK,):
        if str(p) not in sys.path:
            sys.path.insert(0, str(p))
    from db import connect_trade_db  # noqa: E402

    conn = connect_trade_db(str(MYSQL_CONFIG))
    try:
        with conn.cursor() as cur:
            cur.execute(
                """
                SELECT COUNT(*) AS n
                FROM strategy_tasks
                WHERE f_task_type = %s AND f_status = 2
                  AND f_update_time < (NOW() - INTERVAL %s SECOND)
                """,
                (task_type, min_age_sec),
            )
            row = cur.fetchone() or {}
            return int(row.get("n") or 0)
    finally:
        conn.close()


def _p50(values: list[float]) -> float | None:
    if not values:
        return None
    xs = sorted(values)
    mid = len(xs) // 2
    if len(xs) % 2:
        return round(xs[mid], 2)
    return round((xs[mid - 1] + xs[mid]) / 2.0, 2)


def _batch_stats(batch_id: str) -> tuple[int, int, float | None, float | None]:
    sys.path.insert(0, str(ROOT / "bench"))
    from task_latency_stats import load_csv_rows  # noqa: E402

    csv_path = BENCH_LOGS / "task_latency.csv"
    rows = load_csv_rows(csv_path, batch_id=batch_id)
    ok = 0
    fail = 0
    exec_ms: list[float] = []
    sched_ms: list[float] = []
    for row in rows:
        st = str(row.get("final_status") or "")
        if st == "3":
            ok += 1
        else:
            fail += 1
        shell = row.get("ms_shell_start_to_end") or row.get("ms_executor_to_shell_end")
        if shell:
            try:
                exec_ms.append(float(shell))
            except ValueError:
                pass
        ready = row.get("ms_insert_to_ready")
        if ready:
            try:
                sched_ms.append(float(ready))
            except ValueError:
                pass
    return ok, fail, _p50(exec_ms), _p50(sched_ms)


def main() -> int:
    ap = argparse.ArgumentParser(description="Soak hourly sampler")
    ap.add_argument("--soak-id", required=True)
    ap.add_argument("--round", type=int, required=True)
    ap.add_argument("--batch-id", default="")
    ap.add_argument("--round-exit-code", type=int, default=0)
    ap.add_argument("--task-type", type=int, default=4)
    args = ap.parse_args()

    executor_pid = _read_pid(CPP_TASK / "pids/trade_executor_cpp.4.pid")
    worker_pid = _read_pid(PY_TASK / "task_worker.pid")
    executor_rss = _rss_mb(executor_pid) if executor_pid else None
    worker_rss = _rss_mb(worker_pid) if worker_pid else None
    stuck = _stuck_count(task_type=args.task_type)
    ok = fail = 0
    exec_p50 = sched_p50 = None
    if args.batch_id:
        ok, fail, exec_p50, sched_p50 = _batch_stats(args.batch_id)

    out = BENCH_LOGS / f"soak_{args.soak_id}.csv"
    out.parent.mkdir(parents=True, exist_ok=True)
    write_header = not out.is_file()
    row = {
        "sample_ts_bj": datetime.now(TZ_BJ).strftime("%Y-%m-%d %H:%M:%S"),
        "round_no": args.round,
        "batch_id": args.batch_id,
        "round_exit_code": args.round_exit_code,
        "executor_rss_mb": executor_rss if executor_rss is not None else "",
        "worker_rss_mb": worker_rss if worker_rss is not None else "",
        "stuck_f_status2": stuck,
        "tasks_ok": ok,
        "tasks_fail": fail,
        "exec_p50_ms": exec_p50 if exec_p50 is not None else "",
        "sched_p50_ms": sched_p50 if sched_p50 is not None else "",
    }
    with out.open("a", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=CSV_FIELDS)
        if write_header:
            w.writeheader()
        w.writerow(row)

    meta_path = BENCH_LOGS / f"soak_{args.soak_id}_meta.json"
    meta = {}
    if meta_path.is_file():
        meta = json.loads(meta_path.read_text(encoding="utf-8"))
    meta.setdefault("rounds", []).append(
        {
            "round": args.round,
            "batch_id": args.batch_id,
            "exit_code": args.round_exit_code,
            "sample_ts_bj": row["sample_ts_bj"],
        }
    )
    meta_path.write_text(json.dumps(meta, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(row, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
