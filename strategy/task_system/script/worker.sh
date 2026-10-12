#!/usr/bin/env bash
# C++ task worker（与 Python worker 隔离：仅处理 f_task_type=2）

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TASK_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT="$(cd "$TASK_DIR/.." && pwd)"
WORKER_CONFIG="${WORKER_CONFIG:-$TASK_DIR/worker_config.json}"
BUILD="${ROOT}/build"
BIN="${BUILD}/apps/task_worker_cpp"

read_cfg() {
  local key="$1"
  python3 - <<PY 2>/dev/null
import json
from pathlib import Path
p = Path("$WORKER_CONFIG")
data = json.loads(p.read_text(encoding="utf-8"))
v = data.get("$key", None)
if v is None:
    raise SystemExit(1)
print(v)
PY
}

MYSQL_CONFIG="${MYSQL_CONFIG:-$(read_cfg mysql_config)}"
POLL_SEC="${POLL_SEC:-$(read_cfg poll_sec)}"
TASK_TYPE="${TASK_TYPE:-$(read_cfg task_type)}"
SIMULATE="${SIMULATE:-$(read_cfg simulate)}"
SYSTEM_LOG="${SYSTEM_LOG:-$TASK_DIR/sys_log/system_logs_cpp.log}"
PID_FILE="${PID_FILE:-$TASK_DIR/task_worker_cpp.pid}"

export MOOMOO_CPP_ROOT="${ROOT}"
export MOOMOO_CPP_STRATEGY="${ROOT}/strategy"

CMD=(
  "$BIN"
  --mysql-config "$MYSQL_CONFIG"
  --poll-sec "$POLL_SEC"
  --task-type "$TASK_TYPE"
  --system-log "$SYSTEM_LOG"
)
if [ "$SIMULATE" = "1" ]; then
  CMD+=(--simulate)
fi

is_running() {
  [ -f "$PID_FILE" ] || return 1
  local pid
  pid="$(cat "$PID_FILE" 2>/dev/null || true)"
  [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null
}

build_bin() {
  cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DBUILD_TRADE=ON -DBUILD_BENCHES=OFF >/dev/null
  cmake --build "$BUILD" -j "$(nproc)" --target task_worker_cpp
}

case "${1:-status}" in
  start)
    if is_running; then
      echo "[task-worker-cpp] already running pid=$(cat "$PID_FILE")"
      exit 0
    fi
    build_bin
    mkdir -p "$(dirname "$SYSTEM_LOG")"
    nohup "${CMD[@]}" >>"$SYSTEM_LOG" 2>&1 &
    echo $! >"$PID_FILE"
    echo "[task-worker-cpp] started pid=$(cat "$PID_FILE") task_type=$TASK_TYPE"
    ;;
  stop)
    if ! is_running; then
      echo "[task-worker-cpp] not running"
      rm -f "$PID_FILE"
      exit 0
    fi
    kill "$(cat "$PID_FILE")" 2>/dev/null || true
    rm -f "$PID_FILE"
    echo "[task-worker-cpp] stopped"
    ;;
  status)
    if is_running; then
      echo "[task-worker-cpp] running pid=$(cat "$PID_FILE") task_type=$TASK_TYPE"
    else
      echo "[task-worker-cpp] stopped"
    fi
    ;;
  restart)
    "$0" stop || true
    "$0" start
    ;;
  logs)
    tail -f "$SYSTEM_LOG"
    ;;
  once)
    build_bin
    "${CMD[@]}" --once
    ;;
  *)
    echo "usage: $0 {start|stop|status|restart|logs|once}"
    exit 2
    ;;
esac
