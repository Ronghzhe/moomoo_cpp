#!/usr/bin/env bash
# 快捷启动 C++ Executor（Python Worker 负责 prepare）
# 示例:
#   ./run_executor.sh start -n 4
#   ./run_executor.sh start -n 4 -gateway    # C++ gateway + 直连兜底
exec "$(cd "$(dirname "$0")" && pwd)/strategy/task_system/script/executor.sh" "$@"
