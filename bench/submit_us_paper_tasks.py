#!/usr/bin/env python3
"""批量插入美股模拟盘 f_status=1 任务（type=4），等待全链路 entry→exit 并生成延时报告。

用法:
  python3 bench/submit_us_paper_tasks.py
  python3 bench/submit_us_paper_tasks.py --no-wait
  python3 bench/submit_us_paper_tasks.py --rounds 3

默认（config rounds=2）：AAPL/MU/SMCI 各 1 股，每 symbol 2 轮 entry→exit（同任务 requeue exit）。
  共 6 条 DB 任务；CSV 最多 12 行（每任务 entry + exit 各一行）。

前提（需手动启动，本脚本不会 restart）:
  - Python Worker: moomoo_python/strategy/task_system/script/worker.sh restart -late
  - C++ Executor:  moomoo_CPP/strategy/task_system/script/executor.sh restart -n 4 -late
  - 美股模拟盘可交易时段
"""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CONFIG = ROOT / "bench/config/task_bench_us_paper.json"

if str(ROOT / "bench") not in sys.path:
    sys.path.insert(0, str(ROOT / "bench"))

from task_latency_batch_submit import main as batch_main  # noqa: E402


def main() -> int:
    if "--config" not in sys.argv:
        sys.argv[1:1] = ["--config", str(DEFAULT_CONFIG)]
    return batch_main()


if __name__ == "__main__":
    raise SystemExit(main())
