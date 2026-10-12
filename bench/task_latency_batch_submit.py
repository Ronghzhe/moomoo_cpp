#!/usr/bin/env python3
"""批量插入 f_status=1 任务（type=4），等待全链路完成，并从 CSV 生成延时报告。

用法:
  python3 bench/task_latency_batch_submit.py
  python3 bench/task_latency_batch_submit.py --rounds 25
  python3 bench/task_latency_batch_submit.py --parallel-symbols
  python3 bench/task_latency_batch_submit.py --no-wait

默认（config rounds=25）：每个 symbol 各 25 轮 entry→exit（方案 B：同任务 requeue exit）。
  02208×200 / 03308×50，共 50 条 DB 任务 → 50 entry + 50 exit = 100 legs。
  可用 --rounds N 覆盖；腿数 = rounds × len(cases) × 2（含 exit）。

--parallel-symbols / config parallel_symbols=true：
  同轮内多个品种几乎同时插入 entry，全部完成后再并行 requeue exit。
  适合测 gateway 并发排队；默认仍是逐品种串行。

前提（需手动启动，本脚本不会 restart）:
  - Python Worker: task_system/script/worker.sh restart -late
  - C++ Executor:  ./run_executor.sh start -n 4 -gateway   # 或直连不加 -gateway
  - 重新编译并 restart 后，埋点才会生效
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Any, Literal

ROOT = Path(__file__).resolve().parents[1]
PY_TASK = Path("/home/ubuntu/workspace/moomoo_python/strategy/task_system")
CSV_PATH = ROOT / "strategy/task_system/bench_logs/task_latency.csv"
REPORT_DIR = ROOT / "reports"
DEFAULT_CONFIG = ROOT / "bench/config/task_bench_hk_paper.json"
MYSQL_CONFIG = Path("/home/ubuntu/workspace/mysql/mysql_config.json")

LegKind = Literal["entry", "exit"]

for p in (PY_TASK,):
    if str(p) not in sys.path:
        sys.path.insert(0, str(p))

from db import connect_trade_db  # noqa: E402
from task_latency_recorder import init_staging, staging_path, wall_ms  # noqa: E402
from task_params_spec import validate_task_params  # noqa: E402
from task_result import build_f_task_result  # noqa: E402

BENCH_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(BENCH_DIR))
from task_latency_stats import load_csv_rows, write_stats_report  # noqa: E402


def load_cases(config_path: Path) -> dict[str, Any]:
    return json.loads(config_path.read_text(encoding="utf-8"))


def build_payload(
    *,
    stock_code: str,
    trade_qty: int,
    batch_id: str,
    seq: int,
    leg: LegKind,
    round_no: int,
    stock_type: str = "HK",
) -> dict[str, Any]:
    if leg == "entry":
        task_type = "entry_now"
        entry_conditions = [{"s_name": "entry_now", "params": {}}]
        exit_conditions: list[dict[str, Any]] = []
    else:
        task_type = "exit_now"
        entry_conditions = []
        exit_conditions = [{"s_name": "exit_now", "params": {}}]
    return {
        "task_time": wall_ms(),
        "side": "long",
        "order_type": "market",
        "price": None,
        "task_type": task_type,
        "stock_type": stock_type.upper(),
        "stock_code": stock_code,
        "entry_conditions": entry_conditions,
        "exit_conditions": exit_conditions,
        "trade_qty": trade_qty,
        "trade_amount": None,
        "note": f"bench latency batch={batch_id} seq={seq} round={round_no} leg={leg}",
        "_bench": {
            "batch_id": batch_id,
            "seq": seq,
            "round": round_no,
            "leg": leg,
            "stock_code": stock_code,
            "trade_qty": trade_qty,
        },
    }


def expand_bench_steps(cases: list[dict[str, Any]], rounds: int) -> list[tuple[int, str, int]]:
    """round → symbol（每 symbol 一条任务，entry 完成后同任务 requeue exit）。"""
    steps: list[tuple[int, str, int]] = []
    for round_no in range(1, rounds + 1):
        for case in cases:
            stock_code = str(case["stock_code"])
            trade_qty = int(case["trade_qty"])
            steps.append((round_no, stock_code, trade_qty))
    return steps


def expand_bench_rounds(
    cases: list[dict[str, Any]], rounds: int
) -> list[list[tuple[int, str, int]]]:
    """按轮分组：每轮内多个品种可并行插入。"""
    out: list[list[tuple[int, str, int]]] = []
    for round_no in range(1, rounds + 1):
        group: list[tuple[int, str, int]] = []
        for case in cases:
            group.append((round_no, str(case["stock_code"]), int(case["trade_qty"])))
        out.append(group)
    return out


def _write_staging(
    *,
    f_id: int,
    task_name: str,
    payload: dict[str, Any],
    batch_id: str,
    task_type: int,
    seq: int,
    insert_ts_ms: int,
    reset: bool,
    stock_type: str = "HK",
) -> None:
    if reset:
        sp = staging_path(f_id, task_name)
        if sp.is_file():
            sp.unlink()
    stock_code = str(payload["stock_code"])
    trade_qty = int(payload["trade_qty"])
    init_staging(
        f_id=f_id,
        task_name=task_name,
        batch_id=batch_id,
        f_task_type=task_type,
        symbol=f"{stock_type.upper()}.{stock_code}",
        stock_code=stock_code,
        trade_qty=trade_qty,
        insert_ts_ms=insert_ts_ms,
        seq=seq,
    )


def insert_task(
    conn,
    *,
    task_name: str,
    payload: dict[str, Any],
    account: str,
    trader: str,
    task_type: int,
    batch_id: str,
    seq: int,
    stock_type: str = "HK",
) -> int:
    validate_task_params(payload)
    insert_ts = wall_ms()
    payload = dict(payload)
    payload["_bench"] = dict(payload.get("_bench") or {})
    payload["_bench"]["insert_ts_ms"] = insert_ts
    task_json = json.dumps(payload, ensure_ascii=False)
    pending = json.dumps(build_f_task_result(), ensure_ascii=False)
    with conn.cursor() as cur:
        cur.execute(
            """
            INSERT INTO strategy_tasks
            (f_uuid, f_task_type, f_task_name, f_user, f_trader, f_task_params, f_task_result, f_status)
            VALUES (%s, %s, %s, %s, %s, %s, %s, %s)
            """,
            (0, task_type, task_name, account, trader, task_json, pending, 1),
        )
        cur.execute("SELECT LAST_INSERT_ID() AS fid")
        row = cur.fetchone() or {}
        f_id = int(row.get("fid") or 0)
        cur.execute(
            """
            INSERT INTO strategy_logs
            (f_uuid, f_task_type, f_task_name, f_user, f_trader, f_task_params, f_task_result, f_status)
            VALUES (%s, %s, %s, %s, %s, %s, %s, %s)
            """,
            (0, task_type, task_name, account, trader, task_json, "", 1),
        )
    _write_staging(
        f_id=f_id,
        task_name=task_name,
        payload=payload,
        batch_id=batch_id,
        task_type=task_type,
        seq=seq,
        insert_ts_ms=insert_ts,
        reset=False,
        stock_type=stock_type,
    )
    return f_id


def requeue_task_for_exit(
    conn,
    *,
    f_id: int,
    task_name: str,
    payload: dict[str, Any],
    account: str,
    trader: str,
    task_type: int,
    batch_id: str,
    seq: int,
    stock_type: str = "HK",
) -> None:
    """同 task_name 回到 f_status=1，Worker prepare 时 merge 旧 mapped 保留 opened_qty。"""
    validate_task_params(payload)
    insert_ts = wall_ms()
    payload = dict(payload)
    payload["_bench"] = dict(payload.get("_bench") or {})
    payload["_bench"]["insert_ts_ms"] = insert_ts
    task_json = json.dumps(payload, ensure_ascii=False)
    pending = json.dumps(build_f_task_result(), ensure_ascii=False)
    with conn.cursor() as cur:
        cur.execute(
            """
            UPDATE strategy_tasks
            SET f_task_params = %s, f_task_result = %s, f_status = %s
            WHERE f_id = %s AND f_task_name = %s AND f_user = %s
            """,
            (task_json, pending, 1, f_id, task_name, account),
        )
        if int(cur.rowcount or 0) != 1:
            raise RuntimeError(
                f"requeue exit failed: f_id={f_id} task={task_name} f_user={account} rowcount={cur.rowcount}"
            )
        cur.execute(
            """
            INSERT INTO strategy_logs
            (f_uuid, f_task_type, f_task_name, f_user, f_trader, f_task_params, f_task_result, f_status)
            VALUES (%s, %s, %s, %s, %s, %s, %s, %s)
            """,
            (0, task_type, task_name, account, trader, task_json, "", 1),
        )
    _write_staging(
        f_id=f_id,
        task_name=task_name,
        payload=payload,
        batch_id=batch_id,
        task_type=task_type,
        seq=seq,
        insert_ts_ms=insert_ts,
        reset=True,
        stock_type=stock_type,
    )


def wait_tasks(conn, task_names: list[str], timeout_sec: float) -> dict[str, dict[str, Any]]:
    deadline = time.time() + timeout_sec
    pending = set(task_names)
    result: dict[str, dict[str, Any]] = {}
    while pending and time.time() < deadline:
        done_now: list[str] = []
        with conn.cursor() as cur:
            for name in sorted(pending):
                cur.execute(
                    "SELECT f_id, f_status, f_task_result FROM strategy_tasks WHERE f_task_name=%s LIMIT 1",
                    (name,),
                )
                row = cur.fetchone()
                if not row:
                    continue
                st = int(row.get("f_status") or 0)
                if st in (0, 3):
                    result[name] = row
                    done_now.append(name)
                elif st not in (1, 2):
                    result[name] = row
                    done_now.append(name)
        for name in done_now:
            pending.discard(name)
        if pending:
            time.sleep(1.0)
    if pending:
        raise TimeoutError(f"timeout waiting tasks: {sorted(pending)}")
    return result


def read_csv_rows(batch_id: str) -> list[dict[str, str]]:
    return load_csv_rows(CSV_PATH, batch_id=batch_id)


def write_report(batch_id: str, runs: list[tuple[str, LegKind, int]], rows: list[dict[str, str]]) -> Path:
    path = REPORT_DIR / f"task_latency_{batch_id}.md"
    return write_stats_report(
        batch_id,
        rows,
        csv_path=CSV_PATH,
        report_path=path,
        runs=runs,
    )


def main() -> int:
    ap = argparse.ArgumentParser(description="Batch insert paper tasks for latency bench (HK or US)")
    ap.add_argument("--config", default=str(DEFAULT_CONFIG))
    ap.add_argument("--rounds", type=int, default=0, help="override config rounds")
    ap.add_argument("--mysql-config", default=str(MYSQL_CONFIG))
    ap.add_argument("--insert-gap-ms", type=int, default=-1)
    ap.add_argument("--wait-timeout-sec", type=int, default=-1)
    ap.add_argument(
        "--no-wait",
        action="store_true",
        help="insert entry tasks only; skip exit requeue (exit needs entry mapped on disk)",
    )
    ap.add_argument(
        "--parallel-symbols",
        action="store_true",
        help="same round: insert all symbols together, wait all, then requeue exits together",
    )
    ap.add_argument(
        "--sequential-symbols",
        action="store_true",
        help="force one-symbol-at-a-time (overrides config parallel_symbols)",
    )
    args = ap.parse_args()

    cfg = load_cases(Path(args.config))
    rounds = args.rounds or int(cfg.get("rounds") or 3)
    task_type = int(cfg.get("task_type") or 4)
    account = str(cfg.get("account") or "nana_paper")
    trader = str(cfg.get("trader") or "all")
    gap_ms = args.insert_gap_ms if args.insert_gap_ms >= 0 else int(cfg.get("insert_gap_ms") or 0)
    timeout = args.wait_timeout_sec if args.wait_timeout_sec >= 0 else int(cfg.get("wait_timeout_sec") or 900)
    cases = list(cfg.get("cases") or [{"stock_code": "02208", "trade_qty": 200}])
    stock_type = str(cfg.get("stock_type") or "HK").strip().upper()
    bench_tag = stock_type.lower()
    parallel_symbols = bool(cfg.get("parallel_symbols")) or bool(args.parallel_symbols)
    if args.sequential_symbols:
        parallel_symbols = False

    round_groups = expand_bench_rounds(cases, rounds)
    planned_db = sum(len(g) for g in round_groups)
    planned_legs = planned_db if args.no_wait else planned_db * 2
    print(
        f"plan: rounds={rounds} symbols={len(cases)} "
        f"db_tasks={planned_db} legs≈{planned_legs} "
        f"mode={'parallel_symbols' if parallel_symbols else 'sequential'} "
        f"(entry{' only' if args.no_wait else '+exit requeue'})"
    )

    batch_id = datetime.now().strftime("%Y%m%d_%H%M%S")
    runs: list[tuple[str, LegKind, int]] = []
    conn = connect_trade_db(args.mysql_config)
    seq = 0
    try:
        for group in round_groups:
            # --- insert entries (parallel: whole group; sequential: one-by-one with wait) ---
            if parallel_symbols:
                active: list[tuple[str, int, str, int, int]] = []
                # (task_name, f_id, stock_code, trade_qty, round_no)
                for round_no, stock_code, trade_qty in group:
                    seq += 1
                    task_name = f"bench_{bench_tag}_{stock_code}_{batch_id}_{seq:03d}"
                    entry_payload = build_payload(
                        stock_code=stock_code,
                        trade_qty=trade_qty,
                        batch_id=batch_id,
                        seq=seq,
                        leg="entry",
                        round_no=round_no,
                        stock_type=stock_type,
                    )
                    f_id = insert_task(
                        conn,
                        task_name=task_name,
                        payload=entry_payload,
                        account=account,
                        trader=trader,
                        task_type=task_type,
                        batch_id=batch_id,
                        seq=seq,
                        stock_type=stock_type,
                    )
                    runs.append((task_name, "entry", f_id))
                    active.append((task_name, f_id, stock_code, trade_qty, round_no))
                    print(
                        f"inserted f_id={f_id} task={task_name} {stock_type}.{stock_code} "
                        f"qty={trade_qty} leg=entry round={round_no}"
                    )
                    if gap_ms > 0:
                        time.sleep(gap_ms / 1000.0)

                if args.no_wait:
                    continue

                names = [t[0] for t in active]
                done_map = wait_tasks(conn, names, float(timeout))
                exit_names: list[str] = []
                for task_name, f_id, stock_code, trade_qty, round_no in active:
                    st = int((done_map.get(task_name) or {}).get("f_status") or -1)
                    print(f"done entry task={task_name} f_status={st}")
                    if st != 3:
                        print(
                            f"WARN: entry not done (f_status={st}), skip exit requeue: {task_name}",
                            file=sys.stderr,
                        )
                        continue
                    try:
                        exit_seq = int(task_name.rsplit("_", 1)[-1])
                    except ValueError:
                        exit_seq = seq
                    exit_payload = build_payload(
                        stock_code=stock_code,
                        trade_qty=trade_qty,
                        batch_id=batch_id,
                        seq=exit_seq,
                        leg="exit",
                        round_no=round_no,
                        stock_type=stock_type,
                    )
                    requeue_task_for_exit(
                        conn,
                        f_id=f_id,
                        task_name=task_name,
                        payload=exit_payload,
                        account=account,
                        trader=trader,
                        task_type=task_type,
                        batch_id=batch_id,
                        seq=exit_seq,
                        stock_type=stock_type,
                    )
                    runs.append((task_name, "exit", f_id))
                    exit_names.append(task_name)
                    print(f"requeued exit f_id={f_id} task={task_name} f_status=1")
                    if gap_ms > 0:
                        time.sleep(gap_ms / 1000.0)

                if exit_names:
                    exit_done = wait_tasks(conn, exit_names, float(timeout))
                    for task_name in exit_names:
                        st = int((exit_done.get(task_name) or {}).get("f_status") or -1)
                        print(f"done exit task={task_name} f_status={st}")
                        if st != 3:
                            print(
                                f"WARN: exit failed or incomplete f_status={st}: {task_name}",
                                file=sys.stderr,
                            )
            else:
                for round_no, stock_code, trade_qty in group:
                    seq += 1
                    task_name = f"bench_{bench_tag}_{stock_code}_{batch_id}_{seq:03d}"
                    entry_payload = build_payload(
                        stock_code=stock_code,
                        trade_qty=trade_qty,
                        batch_id=batch_id,
                        seq=seq,
                        leg="entry",
                        round_no=round_no,
                        stock_type=stock_type,
                    )
                    f_id = insert_task(
                        conn,
                        task_name=task_name,
                        payload=entry_payload,
                        account=account,
                        trader=trader,
                        task_type=task_type,
                        batch_id=batch_id,
                        seq=seq,
                        stock_type=stock_type,
                    )
                    runs.append((task_name, "entry", f_id))
                    print(
                        f"inserted f_id={f_id} task={task_name} {stock_type}.{stock_code} "
                        f"qty={trade_qty} leg=entry round={round_no}"
                    )
                    if gap_ms > 0:
                        time.sleep(gap_ms / 1000.0)

                    if args.no_wait:
                        continue

                    row = wait_tasks(conn, [task_name], float(timeout))[task_name]
                    st = int(row.get("f_status") or -1)
                    print(f"done entry task={task_name} f_status={st}")
                    if st != 3:
                        print(
                            f"WARN: entry not done (f_status={st}), skip exit requeue: {task_name}",
                            file=sys.stderr,
                        )
                        continue

                    exit_payload = build_payload(
                        stock_code=stock_code,
                        trade_qty=trade_qty,
                        batch_id=batch_id,
                        seq=seq,
                        leg="exit",
                        round_no=round_no,
                        stock_type=stock_type,
                    )
                    requeue_task_for_exit(
                        conn,
                        f_id=f_id,
                        task_name=task_name,
                        payload=exit_payload,
                        account=account,
                        trader=trader,
                        task_type=task_type,
                        batch_id=batch_id,
                        seq=seq,
                        stock_type=stock_type,
                    )
                    runs.append((task_name, "exit", f_id))
                    print(f"requeued exit f_id={f_id} task={task_name} f_status=1")

                    row = wait_tasks(conn, [task_name], float(timeout))[task_name]
                    st = int(row.get("f_status") or -1)
                    print(f"done exit task={task_name} f_status={st}")
                    if st != 3:
                        print(
                            f"WARN: exit failed or incomplete f_status={st}: {task_name}",
                            file=sys.stderr,
                        )
    finally:
        conn.close()

    db_tasks = len({r[0] for r in runs if r[1] == "entry"})
    print(
        f"batch_id={batch_id} db_tasks={db_tasks} legs={len(runs)} rounds={rounds} "
        f"f_status=1 task_type={task_type} "
        f"mode={'parallel_symbols' if parallel_symbols else 'sequential'}"
    )

    rows = read_csv_rows(batch_id)
    report = write_report(batch_id, runs, rows)
    print(f"report={report}")
    print(f"csv={CSV_PATH}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())