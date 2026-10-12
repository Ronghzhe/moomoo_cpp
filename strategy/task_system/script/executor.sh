#!/usr/bin/env bash
# C++ trade executor：配合 Python Worker，拉起 strategy_cpp。
# 启动方式由命令行指定：-n f_task_type，-s 认领的 f_status（默认 2）。
# -gateway：走 C++ opend_gateway（:18766）报单，失败时直连兜底。

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TASK_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT="$(cd "$TASK_DIR/../.." && pwd)"
EXECUTOR_CONFIG="${EXECUTOR_CONFIG:-$TASK_DIR/executor_config.json}"
BUILD="${ROOT}/build"
BIN="${BUILD}/apps/trade_executor_cpp"
GATEWAY_CTL="${GATEWAY_CTL:-$ROOT/scripts/gateway_cpp.sh}"

ACTION=""
TASK_TYPE=""
CLAIM_STATUS="2"
EXECUTOR_LATE=0
USE_GATEWAY=0
EXTRA_ARGS=()

read_cfg() {
  local key="$1"
  local fallback="${2:-}"
  if [ ! -f "$EXECUTOR_CONFIG" ]; then
    echo "$fallback"
    return 0
  fi
  python3 - <<PY 2>/dev/null || echo "$fallback"
import json
from pathlib import Path
p = Path("$EXECUTOR_CONFIG")
data = json.loads(p.read_text(encoding="utf-8"))
v = data.get("$key", None)
if v is None:
    raise SystemExit(1)
print(v)
PY
}

usage() {
  cat <<EOF
Usage: $(basename "$0") start -n <f_task_type> [-s <claim_f_status>] [-late] [-gateway]
       $(basename "$0") stop -n <f_task_type>
       $(basename "$0") status -n <f_task_type>
       $(basename "$0") restart -n <f_task_type> [-s <claim_f_status>] [-late] [-gateway]
       $(basename "$0") once -n <f_task_type> [-s <claim_f_status>] [-late] [-gateway]
       $(basename "$0") logs

选项（启动时必须指定队列，不写 config）:
  -n, --task-type N     f_task_type（例如 1=Python 生产队列）
  -s, --claim-status N  认领 f_status=N 的任务（默认 2，Worker prepare 后）
  -late, --late         bench 用：关闭 open_stagger 节流（测延时勿人为 sleep）
  -gateway              启用 C++ opend_gateway 报单（gateway 优先，失败直连兜底）
                        默认 URL http://127.0.0.1:18766；gateway 由 scripts/gateway_cpp.sh 独立管理，
                        executor stop 不会停止 gateway

示例:
  $(basename "$0") start -n 4                      # 直连 OpenD
  $(basename "$0") start -n 4 -gateway             # gateway + 直连兜底
  ./run_executor.sh start -n 4 -gateway            # 同上（仓库根快捷入口）

环境变量（可选）: MYSQL_CONFIG MOOMOO_TASK_BACKUP_DIR EXECUTOR_POLL_SEC OPEND_GATEWAY_URL
基础设施配置（poll/timeout 等）见: $EXECUTOR_CONFIG
EOF
}

parse_args() {
  ACTION="${1:-status}"
  shift || true
  ACTION="${ACTION#-}"

  while [ "$#" -gt 0 ]; do
    case "$1" in
      -n|--task-type)
        TASK_TYPE="${2:-}"
        shift 2
        ;;
      -s|--claim-status)
        CLAIM_STATUS="${2:-}"
        shift 2
        ;;
      -late|--late)
        EXECUTOR_LATE=1
        shift 1
        ;;
      -gateway)
        USE_GATEWAY=1
        shift 1
        ;;
      -h|--help)
        usage
        exit 0
        ;;
      *)
        EXTRA_ARGS+=("$1")
        shift 1
        ;;
    esac
  done
}

# Export place-broker env for trade_executor_cpp → strategy_cpp children.
apply_gateway_env() {
  if [ "$USE_GATEWAY" != "1" ]; then
    unset ORDER_BROKER_MODE || true
    return 0
  fi
  export ORDER_BROKER_MODE=gateway_fallback
  export OPEND_GATEWAY_URL="${OPEND_GATEWAY_URL:-http://127.0.0.1:18766}"
  export OPEND_GATEWAY_HTTP_TIMEOUT_SEC="${OPEND_GATEWAY_HTTP_TIMEOUT_SEC:-18}"
}

ensure_gateway_running() {
  if [ "$USE_GATEWAY" != "1" ]; then
    return 0
  fi
  if [ ! -x "$GATEWAY_CTL" ]; then
    echo "[trade-executor-cpp] gateway ctl missing: $GATEWAY_CTL" >&2
    exit 1
  fi
  # Live place into OpenD; dry-run only if explicitly requested.
  GATEWAY_CPP_MODE="${GATEWAY_CPP_MODE:---live}" "$GATEWAY_CTL" start
}

require_task_type() {
  if [ -z "$TASK_TYPE" ]; then
    echo "[trade-executor-cpp] 缺少 -n <f_task_type>。示例: $(basename "$0") $ACTION -n 1 [-s 2]" >&2
    usage >&2
    exit 2
  fi
}

parse_args "$@"

MYSQL_CONFIG="${MYSQL_CONFIG:-$(read_cfg mysql_config /home/ubuntu/workspace/mysql/mysql_config.json)}"
POLL_SEC="${EXECUTOR_POLL_SEC:-$(read_cfg executor_poll_sec 0.2)}"
PARALLEL="${EXECUTOR_PARALLEL:-$(read_cfg executor_parallel 3)}"
PARALLEL_HK="${EXECUTOR_PARALLEL_HK:-$(read_cfg executor_parallel_hk 3)}"
PARALLEL_US="${EXECUTOR_PARALLEL_US:-$(read_cfg executor_parallel_us 3)}"
NOT_DONE_RETRY_SEC="${EXECUTOR_NOT_DONE_RETRY_SEC:-$(read_cfg executor_not_done_retry_sec 30)}"
TIMEOUT_SEC="${EXECUTOR_TIMEOUT_SEC:-$(read_cfg executor_timeout_sec 0)}"
OPEN_STAGGER_SLOT="${EXECUTOR_OPEN_STAGGER_SLOT_SEC:-$(read_cfg executor_open_stagger_slot_sec 2)}"
OPEN_STAGGER_BUCKETS="${EXECUTOR_OPEN_STAGGER_BUCKETS:-$(read_cfg executor_open_stagger_buckets 20)}"
if [ "$EXECUTOR_LATE" = "1" ]; then
  OPEN_STAGGER_SLOT=0
  OPEN_STAGGER_BUCKETS=0
  export MOOMOO_BENCH_HOT_PATH=1
fi
SYSTEM_LOG="${SYSTEM_LOG:-$TASK_DIR/sys_log/executor_cpp.${TASK_TYPE:-x}.log}"
PID_FILE="${PID_FILE:-$TASK_DIR/pids/trade_executor_cpp.${TASK_TYPE:-x}.pid}"

export MOOMOO_CPP_ROOT="${ROOT}"
export MOOMOO_CPP_STRATEGY="${ROOT}/strategy"
export MOOMOO_PYTHON_STRATEGY="${MOOMOO_PYTHON_STRATEGY:-/home/ubuntu/workspace/moomoo_python/strategy}"
export MOOMOO_TASK_BACKUP_DIR="${MOOMOO_TASK_BACKUP_DIR:-$MOOMOO_PYTHON_STRATEGY/config/backups}"

build_bin() {
  cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DBUILD_TRADE=ON -DBUILD_BENCHES=OFF >/dev/null
  cmake --build "$BUILD" -j "$(nproc)" --target trade_executor_cpp
}

is_running() {
  [ -f "$PID_FILE" ] || return 1
  local pid
  pid="$(cat "$PID_FILE" 2>/dev/null || true)"
  [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null
}

run_cmd() {
  "$BIN" \
    --mysql-config "$MYSQL_CONFIG" \
    --task-type "$TASK_TYPE" \
    --claim-status "$CLAIM_STATUS" \
    --poll-sec "$POLL_SEC" \
    --parallel "$PARALLEL" \
    --parallel-hk "$PARALLEL_HK" \
    --parallel-us "$PARALLEL_US" \
    --not-done-retry-sec "$NOT_DONE_RETRY_SEC" \
    --timeout-sec "$TIMEOUT_SEC" \
    --open-stagger-slot-sec "$OPEN_STAGGER_SLOT" \
    --open-stagger-buckets "$OPEN_STAGGER_BUCKETS" \
    --system-log "$SYSTEM_LOG" \
    --pid-file "$PID_FILE" \
    "$@"
}

case "$ACTION" in
  start)
    require_task_type
    SYSTEM_LOG="$TASK_DIR/sys_log/executor_cpp.${TASK_TYPE}.log"
    PID_FILE="$TASK_DIR/pids/trade_executor_cpp.${TASK_TYPE}.pid"
    if is_running; then
      echo "[trade-executor-cpp] already running pid=$(cat "$PID_FILE") task_type=$TASK_TYPE claim_status=$CLAIM_STATUS"
      exit 0
    fi
    build_bin
    mkdir -p "$(dirname "$SYSTEM_LOG")" "$(dirname "$PID_FILE")" "$TASK_DIR/run_logs"
    echo "[trade-executor-cpp] starting task_type=$TASK_TYPE claim_status=$CLAIM_STATUS"
    echo "  parallel=$PARALLEL poll_sec=$POLL_SEC parallel_hk=$PARALLEL_HK parallel_us=$PARALLEL_US not_done_retry=${NOT_DONE_RETRY_SEC}s"
    if [ "$EXECUTOR_LATE" = "1" ]; then
      echo "  open_stagger=off (-late bench)"
      echo "  bench_hot_path=MOOMOO_BENCH_HOT_PATH=1 (skip Connect/Place 200ms sleeps; OpenD Connect flock on)"
    else
      echo "  open_stagger=${OPEN_STAGGER_SLOT}s x ${OPEN_STAGGER_BUCKETS} buckets"
    fi
    if [ "${MOOMOO_STRATEGY_POOL:-1}" != "0" ]; then
      echo "  strategy_pool=on (N=parallel sticky InitQot/InitTrd; MOOMOO_STRATEGY_POOL=0 to disable)"
    else
      echo "  strategy_pool=off (shell per task)"
    fi
    if [ "$USE_GATEWAY" = "1" ]; then
      ensure_gateway_running
      apply_gateway_env
      echo "  order_mode=gateway_fallback (C++ :18766 + direct fallback)"
      echo "  gateway_url=$OPEND_GATEWAY_URL"
    else
      echo "  order_mode=direct"
    fi
    echo "  backup_dir=$MOOMOO_TASK_BACKUP_DIR"
    echo "  run_logs=$TASK_DIR/run_logs"
    echo "  log=$SYSTEM_LOG"
    nohup "$BIN" \
      --mysql-config "$MYSQL_CONFIG" \
      --task-type "$TASK_TYPE" \
      --claim-status "$CLAIM_STATUS" \
      --poll-sec "$POLL_SEC" \
      --parallel "$PARALLEL" \
      --parallel-hk "$PARALLEL_HK" \
      --parallel-us "$PARALLEL_US" \
      --not-done-retry-sec "$NOT_DONE_RETRY_SEC" \
      --timeout-sec "$TIMEOUT_SEC" \
      --open-stagger-slot-sec "$OPEN_STAGGER_SLOT" \
      --open-stagger-buckets "$OPEN_STAGGER_BUCKETS" \
      --system-log "$SYSTEM_LOG" \
      --pid-file "$PID_FILE" \
      >>"$SYSTEM_LOG" 2>&1 &
    sleep 0.5
    if is_running; then
      echo "[trade-executor-cpp] started pid=$(cat "$PID_FILE")"
    else
      echo "[trade-executor-cpp] failed to start; see $SYSTEM_LOG" >&2
      exit 1
    fi
    ;;
  stop)
    require_task_type
    PID_FILE="$TASK_DIR/pids/trade_executor_cpp.${TASK_TYPE}.pid"
    if ! is_running; then
      echo "[trade-executor-cpp] not running (task_type=$TASK_TYPE)"
      rm -f "$PID_FILE"
      exit 0
    fi
    kill "$(cat "$PID_FILE")" 2>/dev/null || true
    rm -f "$PID_FILE"
    echo "[trade-executor-cpp] stopped task_type=$TASK_TYPE"
    echo "[trade-executor-cpp] note: C++ gateway (if any) is not stopped; use scripts/gateway_cpp.sh stop"
    ;;
  status)
    require_task_type
    PID_FILE="$TASK_DIR/pids/trade_executor_cpp.${TASK_TYPE}.pid"
    if is_running; then
      echo "[trade-executor-cpp] running pid=$(cat "$PID_FILE") task_type=$TASK_TYPE claim_status=$CLAIM_STATUS"
    else
      echo "[trade-executor-cpp] stopped task_type=$TASK_TYPE"
    fi
    if [ -x "$GATEWAY_CTL" ]; then
      "$GATEWAY_CTL" status || true
    fi
    ;;
  restart)
    require_task_type
    "$0" stop -n "$TASK_TYPE" || true
    RESTART_ARGS=(-n "$TASK_TYPE")
    if [ -n "$CLAIM_STATUS" ] && [ "$CLAIM_STATUS" != "2" ]; then
      RESTART_ARGS+=(-s "$CLAIM_STATUS")
    fi
    if [ "$EXECUTOR_LATE" = "1" ]; then
      RESTART_ARGS+=(-late)
    fi
    if [ "$USE_GATEWAY" = "1" ]; then
      RESTART_ARGS+=(-gateway)
    fi
    "$0" start "${RESTART_ARGS[@]}"
    ;;
  logs)
    if [ -n "$TASK_TYPE" ]; then
      SYSTEM_LOG="$TASK_DIR/sys_log/executor_cpp.${TASK_TYPE}.log"
    fi
    if [ ! -f "$SYSTEM_LOG" ]; then
      echo "[trade-executor-cpp] log not found: $SYSTEM_LOG (try: logs -n <task_type>)" >&2
      exit 1
    fi
    tail -f "$SYSTEM_LOG"
    ;;
  once)
    require_task_type
    build_bin
    if [ "$USE_GATEWAY" = "1" ]; then
      ensure_gateway_running
      apply_gateway_env
    fi
    run_cmd --once
    ;;
  *)
    usage
    exit 2
    ;;
esac
