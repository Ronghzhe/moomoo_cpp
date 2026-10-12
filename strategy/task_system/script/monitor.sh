#!/usr/bin/env bash
# C++ task_system 监控：opend_gateway_cpp + trade_executor_cpp + strategy_cpp
# Python Worker 仅展示引用状态（prepare 仍由 Python 负责）

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TASK_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT="$(cd "$TASK_DIR/../.." && pwd)"
PID_ROOT="$TASK_DIR/pids"
PY_TASK_DIR="${MOOMOO_PYTHON_TASK_DIR:-$(cd "$ROOT/../moomoo_python/strategy/task_system" 2>/dev/null && pwd || echo "")}"
PY_WORKER_PID="${PY_WORKER_PID_FILE:-${PY_TASK_DIR}/task_worker.pid}"
VERBOSE=0
TASK_TYPE_FILTER=""

if [ "${1:-}" = "--verbose" ] || [ "${1:-}" = "-v" ]; then
  VERBOSE=1
  shift || true
fi

while [ "$#" -gt 0 ]; do
  case "$1" in
    -n|--task-type)
      TASK_TYPE_FILTER="${2:-}"
      shift 2
      ;;
    -h|--help)
      cat <<EOF
Usage: $(basename "$0") [-n <f_task_type>|all] [--verbose]

  -n 1       只看 task_type=1 的 C++ executor
  -n all     列出全部 trade_executor_cpp.* 实例（默认）
  --verbose  附加 tail 日志

说明: Python Worker / Python gateway(:18765) 见 moomoo_python/.../monitor.sh
EOF
      exit 0
      ;;
    *)
      echo "[monitor-cpp] unknown arg: $1" >&2
      exit 2
      ;;
  esac
done

ppid_of() {
  awk '/^PPid:/{print $2}' "/proc/$1/status" 2>/dev/null || true
}

proc_state() {
  local name="$1"
  local pid_file="$2"
  if [ ! -f "$pid_file" ]; then
    echo "$name: stopped (no pid file)"
    return
  fi
  local pid
  pid="$(tr -d ' \t\r\n' <"$pid_file")"
  if [ -z "$pid" ]; then
    echo "$name: stopped (empty pid file)"
    return
  fi
  if kill -0 "$pid" 2>/dev/null; then
    echo "$name: running (pid=$pid)"
  else
    echo "$name: stopped (stale pid=$pid)"
  fi
}

executor_cmdline_info() {
  local pf="$1"
  local label="$2"
  if [ ! -f "$pf" ]; then
    echo "  cmdline($label): (no pid file)"
    return
  fi
  local epid
  epid="$(tr -d ' \t\r\n' <"$pf")"
  if [ -z "$epid" ] || ! kill -0 "$epid" 2>/dev/null; then
    echo "  cmdline($label): (not running)"
    return
  fi
  local cl tt cs
  cl=$(tr '\0' ' ' <"/proc/$epid/cmdline" 2>/dev/null || true)
  tt=$(printf '%s\n' "$cl" | sed -n 's/.*--task-type[[:space:]]\([0-9][0-9]*\).*/\1/p' | head -1)
  cs=$(printf '%s\n' "$cl" | sed -n 's/.*--claim-status[[:space:]]\([0-9][0-9]*\).*/\1/p' | head -1)
  echo "  cmdline($label): pid=$epid task_type=${tt:-?} claim_status=${cs:-?}"
}

list_executor_instances() {
  local found=0
  shopt -s nullglob
  local pattern="$PID_ROOT/trade_executor_cpp.*.pid"
  if [ -n "$TASK_TYPE_FILTER" ] && [ "$TASK_TYPE_FILTER" != "all" ]; then
    pattern="$PID_ROOT/trade_executor_cpp.${TASK_TYPE_FILTER}.pid"
  fi
  for f in $pattern; do
    found=1
    local bn tt
    bn=$(basename "$f")
    tt="${bn#trade_executor_cpp.}"
    tt="${tt%.pid}"
    echo "executor_cpp(f_task_type=${tt}):"
    proc_state "  process" "$f"
    executor_cmdline_info "$f" "type=${tt}"
    local logf="$TASK_DIR/sys_log/executor_cpp.${tt}.log"
    if [ -f "$logf" ]; then
      local last
      last="$(grep -E 'executor_run|executor_done|scan_idle|executor_daemon_boot' "$logf" 2>/dev/null | tail -1 || true)"
      if [ -n "$last" ]; then
        echo "  last_log: $last"
      fi
    fi
    echo
  done
  shopt -u nullglob
  if [ "$found" -eq 0 ]; then
    if [ -n "$TASK_TYPE_FILTER" ] && [ "$TASK_TYPE_FILTER" != "all" ]; then
      echo "executor_cpp(f_task_type=${TASK_TYPE_FILTER}): stopped (no $PID_ROOT/trade_executor_cpp.${TASK_TYPE_FILTER}.pid)"
    else
      echo "executor_cpp: stopped (no $PID_ROOT/trade_executor_cpp.<n>.pid)"
    fi
  fi
}

strategy_cpp_state() {
  local pids=""
  if command -v pgrep >/dev/null 2>&1; then
    pids="$(pgrep -f "${ROOT}/build/apps/strategy_cpp" 2>/dev/null || true)"
    if [ -z "$pids" ]; then
      pids="$(pgrep -f "strategy_cpp" 2>/dev/null | grep -v trade_executor || true)"
    fi
  fi
  if [ -z "$pids" ]; then
    echo "strategy_cpp: stopped (no active task)"
    return
  fi
  local count=0
  local lines=()
  for pid in $pids; do
    pid="$(printf '%s' "$pid" | tr -d ' \t\r\n')"
    [ -z "$pid" ] && continue
    kill -0 "$pid" 2>/dev/null || continue
    count=$((count + 1))
    local cl cfg mode
    cl=$(tr '\0' ' ' <"/proc/$pid/cmdline" 2>/dev/null || true)
    cfg=$(printf '%s\n' "$cl" | sed -n 's/.*-c[[:space:]]\([^[:space:]]*\).*/\1/p; s/.*--config[[:space:]]\([^[:space:]]*\).*/\1/p' | head -1)
    if [ -n "$cfg" ] && [ -f "$cfg" ] && command -v python3 >/dev/null 2>&1; then
      mode="$(python3 - <<PY 2>/dev/null || true
import json
from pathlib import Path
p = Path(r"""$cfg""")
print(str(json.loads(p.read_text(encoding="utf-8")).get("mode", "")).strip().lower())
PY
)"
    else
      mode="unknown"
    fi
    lines+=("       $pid  mode=$mode  config=${cfg:-unknown}")
  done
  if [ "$count" -eq 0 ]; then
    echo "strategy_cpp: stopped"
    return
  fi
  echo "strategy_cpp ($count running):"
  for line in "${lines[@]}"; do
    echo "$line"
  done
}

python_worker_ref() {
  if [ -z "$PY_TASK_DIR" ] || [ ! -d "$PY_TASK_DIR" ]; then
    echo "python_worker: (path not found, set MOOMOO_PYTHON_TASK_DIR)"
    return
  fi
  proc_state "python_worker(ref)" "$PY_WORKER_PID"
  echo "  hint: prepare 由 Python Worker 负责"
}

gateway_cpp_ref() {
  local ctl="${GATEWAY_CPP_CTL:-$ROOT/scripts/gateway_cpp.sh}"
  echo "opend_gateway_cpp (127.0.0.1:18766, executor -gateway):"
  if [ ! -f "$ctl" ]; then
    echo "  ctl: missing ($ctl)"
    return
  fi
  bash "$ctl" status 2>&1 | sed 's/^\[opend-gateway-cpp\] /  /' || echo "  status failed"
  if [ "$VERBOSE" -eq 1 ]; then
    local logf="${GATEWAY_CPP_LOG:-$ROOT/sys_log/gateway_cpp/gateway.log}"
    if [ -f "$logf" ]; then
      local tail1
      tail1="$(tail -n 1 "$logf" 2>/dev/null || true)"
      if [ -n "$tail1" ]; then
        echo "  log_tail: $tail1"
      fi
    fi
  fi
}

last_task_paths_from_executor() {
  local run_log="" mapped="" executor_line=""
  shopt -s nullglob
  for f in "$TASK_DIR"/sys_log/executor_cpp.*.log; do
    [ -f "$f" ] || continue
    if [ -n "$TASK_TYPE_FILTER" ] && [ "$TASK_TYPE_FILTER" != "all" ]; then
      case "$f" in
        *".${TASK_TYPE_FILTER}.log") ;;
        *) continue ;;
      esac
    fi
    local line rl mp
    line="$(grep -E 'executor_run task=' "$f" 2>/dev/null | tail -1 || true)"
    if [ -n "$line" ]; then
      executor_line="$line"
      rl=$(printf '%s\n' "$line" | sed -n 's/.*run_log=\([^ ]*\).*/\1/p')
      mp=$(printf '%s\n' "$line" | sed -n 's/.*-c \([^ ]*_mapped\.json\).*/\1/p')
      [ -n "$rl" ] && run_log="$rl"
      [ -n "$mp" ] && mapped="$mp"
    fi
    line="$(grep -E '"mapped_backup"' "$f" 2>/dev/null | tail -1 || true)"
    if [ -n "$line" ]; then
      mp=$(printf '%s\n' "$line" | sed -n 's/.*"mapped_backup"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p')
      [ -n "$mp" ] && mapped="$mp"
    fi
  done
  shopt -u nullglob
  PY_BACKUP="${MOOMOO_TASK_BACKUP_DIR:-${ROOT}/../moomoo_python/strategy/config/backups}"
  echo "mapped_backup_dir: $PY_BACKUP"
  if [ -n "$mapped" ]; then
    echo "last_mapped_json: $mapped"
  else
    echo "last_mapped_json: (none in executor log)"
  fi
  if [ -n "$run_log" ]; then
    echo "last_task_run_log: $run_log"
  else
    echo "last_task_run_log: (none)"
  fi
}

echo "=== moomoo_CPP task_system monitor ==="
echo "time: $(date '+%F %T %Z')"
echo "root: $ROOT"
echo
python_worker_ref
echo
gateway_cpp_ref
echo
list_executor_instances
strategy_cpp_state
echo
last_task_paths_from_executor

if [ "$VERBOSE" -eq 1 ]; then
  echo
  echo "=== paths ==="
  echo "pid_root: $PID_ROOT"
  echo "executor_binary: $ROOT/build/apps/trade_executor_cpp"
  echo "strategy_binary: $ROOT/build/apps/strategy_cpp"
  echo "gateway_ctl: ${GATEWAY_CPP_CTL:-$ROOT/scripts/gateway_cpp.sh}"
  echo "gateway_health: http://127.0.0.1:18766/health"
  echo "backup_dir: ${MOOMOO_TASK_BACKUP_DIR:-${MOOMOO_PYTHON_STRATEGY:-../moomoo_python/strategy}/config/backups}"
  echo
  echo "=== tail executor log ==="
  shopt -s nullglob
  for f in "$TASK_DIR"/sys_log/executor_cpp.*.log; do
    if [ -n "$TASK_TYPE_FILTER" ] && [ "$TASK_TYPE_FILTER" != "all" ]; then
      case "$f" in *".${TASK_TYPE_FILTER}.log") ;; *) continue ;; esac
    fi
    echo "# $f"
    tail -n 15 "$f" 2>/dev/null || true
    echo
  done
  shopt -u nullglob
fi
