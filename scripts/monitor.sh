#!/usr/bin/env bash
# 监控 moomoo_CPP 策略进程（默认精简；--verbose 查看详细）
# 对齐 moomoo_python/strategy/task_system/script/monitor.sh 风格。

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PY_ROOT="${MOOMOO_PYTHON_ROOT:-$(cd "${ROOT}/../moomoo_python" && pwd)}"
PID_ROOT="${PID_ROOT:-$ROOT/pids}"
STRATEGY_PID_FILE="${STRATEGY_PID_FILE:-$PID_ROOT/strategy_cpp.pid}"
WORKER_PID_FILE="${WORKER_PID_FILE:-$PID_ROOT/task_worker_cpp.pid}"
EXECUTOR_PID_FILE="${EXECUTOR_PID_FILE:-$PID_ROOT/trade_executor_cpp.pid}"
CONFIG_PATH="${STRATEGY_CONFIG:-${STRATEGY_CONFIG_PATH:-$ROOT/strategy/config/strategy_config_hk.json}}"
TG_CONFIG="${TG_CONFIG:-$PY_ROOT/telegram/telegram_config.json}"
TG_SECRETS="${TG_SECRETS:-$PY_ROOT/secrets/tg_bot}"
VERBOSE=0

if [ "${1:-}" = "--verbose" ] || [ "${1:-}" = "-v" ]; then
  VERBOSE=1
fi

ppid_of() {
  awk '/^PPid:/{print $2}' "/proc/$1/status" 2>/dev/null || true
}

proc_state() {
  local name="$1"
  local pid_file="$2"
  if [ -n "${pid_file:-}" ] && [ -f "$pid_file" ]; then
    local pid
    pid="$(tr -d ' \t\r\n' <"$pid_file")"
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
      echo "$name: running (pid=$pid)"
      return
    fi
    if [ -n "$pid" ]; then
      echo "$name: stopped (stale pid=$pid)"
      return
    fi
  fi
  echo "$name: stopped"
}

strategy_process_state() {
  local pid=""
  if [ -f "$STRATEGY_PID_FILE" ]; then
    pid="$(tr -d ' \t\r\n' <"$STRATEGY_PID_FILE")"
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
      echo "strategy_cpp: running (pid=$pid, pid_file=${STRATEGY_PID_FILE##*/})"
      return
    fi
  fi
  if command -v pgrep >/dev/null 2>&1; then
    pid="$(pgrep -f "${ROOT}/build/apps/strategy_cpp" 2>/dev/null | head -1 || true)"
    if [ -z "$pid" ]; then
      pid="$(pgrep -f "strategy_cpp" 2>/dev/null | head -1 || true)"
    fi
  else
    pid="$(
      ps -eo pid=,args= 2>/dev/null | grep -F "strategy_cpp" | grep -v grep | awk '{print $1}' | head -1 || true
    )"
  fi
  pid="$(printf '%s' "$pid" | tr -d ' \t\r\n')"
  if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
    echo "strategy_cpp: running (pid=$pid)"
  elif [ -f "$STRATEGY_PID_FILE" ]; then
    echo "strategy_cpp: stopped (stale pid file ${STRATEGY_PID_FILE##*/})"
  else
    echo "strategy_cpp: stopped"
  fi
}

strategy_cmdline_info() {
  local pid=""
  if command -v pgrep >/dev/null 2>&1; then
    pid="$(pgrep -f "${ROOT}/build/apps/strategy_cpp" 2>/dev/null | head -1 || true)"
    if [ -z "$pid" ]; then
      pid="$(pgrep -f "strategy_cpp" 2>/dev/null | head -1 || true)"
    fi
  fi
  pid="$(printf '%s' "$pid" | tr -d ' \t\r\n')"
  if [ -z "$pid" ] || ! kill -0 "$pid" 2>/dev/null; then
    echo "strategy_cmdline: (not running)"
    return
  fi
  local cl cfg mode dry
  cl=$(tr '\0' ' ' <"/proc/$pid/cmdline" 2>/dev/null || true)
  cfg=$(printf '%s\n' "$cl" | sed -n 's/.*-c[[:space:]]\([^[:space:]]*\).*/\1/p; s/.*--config[[:space:]]\([^[:space:]]*\).*/\1/p' | head -1)
  if printf '%s' "$cl" | grep -qE '(^|[[:space:]])--dry-run([[:space:]]|$)|--judge-only'; then
    dry="dry_run"
  else
    dry="place_orders"
  fi
  if [ -n "$cfg" ] && [ -f "$cfg" ] && command -v python3 >/dev/null 2>&1; then
    mode="$(python3 - <<PY 2>/dev/null || true
import json
from pathlib import Path
p = Path(r"""$cfg""")
print(str(json.loads(p.read_text(encoding="utf-8")).get("mode", "")).strip().lower())
PY
)"
  else
    mode=""
  fi
  echo "strategy_cmdline: pid=$pid mode=${mode:-unknown} order_mode=$dry config=${cfg:-unknown}"
}

strategy_screen_info() {
  local pid=""
  if command -v pgrep >/dev/null 2>&1; then
    pid="$(pgrep -f "strategy_cpp" 2>/dev/null | head -1 || true)"
  fi
  pid="$(printf '%s' "$pid" | tr -d ' \t\r\n')"
  if [ -z "$pid" ]; then
    echo "strategy_screen: (no strategy_cpp process)"
    return
  fi
  local cur="$pid"
  local i=0
  while [ -n "$cur" ] && [ "$cur" != "0" ] && [ "$i" -lt 40 ]; do
    if [ -r "/proc/$cur/environ" ]; then
      local sty
      sty=$(tr '\0' '\n' <"/proc/$cur/environ" 2>/dev/null | grep '^STY=' | head -1 | cut -d= -f2- || true)
      if [ -n "$sty" ]; then
        echo "strategy_screen: $sty (strategy pid=$pid)"
        return
      fi
    fi
    cur=$(ppid_of "$cur")
    i=$((i + 1))
  done
  echo "strategy_screen: (no STY — not under GNU screen, strategy pid=$pid)"
}

read_config_summary() {
  if [ ! -f "$CONFIG_PATH" ]; then
    echo "config: missing ($CONFIG_PATH)"
    return
  fi
  if ! command -v python3 >/dev/null 2>&1; then
    echo "config: $CONFIG_PATH (python3 required for summary)"
    return
  fi
  MOOMOO_CPP_ROOT="$ROOT" python3 - <<PY
import json, os
from pathlib import Path

root = Path(os.environ.get("MOOMOO_CPP_ROOT", ".")).resolve()
cfg_path = Path(r"""$CONFIG_PATH""")
if not cfg_path.is_absolute():
    cfg_path = (Path.cwd() / cfg_path).resolve()
cfg = json.loads(cfg_path.read_text(encoding="utf-8"))
mode = str(cfg.get("mode", "paper")).strip().lower()
csv_raw = str((cfg.get("storage") or {}).get("csv_dir") or cfg.get("csv_dir") or "./strategy/daily_logs")
if not csv_raw.startswith("/"):
    csv_dir = (root / csv_raw).resolve()
else:
    csv_dir = Path(csv_raw)
acc = cfg.get("account") or {}
acc_id = acc.get("acc_id", 0)
acc_index = acc.get("acc_index", 0)
print(f"config: {cfg_path}")
print(f"mode: {mode}")
print(f"csv_dir: {csv_dir}")
print(f"account: acc_{acc_id}_idx_{acc_index}")
symbols = cfg.get("symbols") or []
enabled = [s for s in symbols if s.get("enabled", True)]
print(f"symbols: {len(symbols)} total, {len(enabled)} enabled")
for s in enabled[:8]:
    code = s.get("code", "?")
    status = s.get("status", 0)
    oq = s.get("opened_qty", 0)
    op = s.get("opened_price", 0)
    updated = s.get("runtime_updated_at", "")
    tag = code.replace(".", "_")
    log_dir = csv_dir / f"acc_{acc_id}_idx_{acc_index}" / tag
    runtime_log = log_dir / "runtime.log"
    extra = f" updated_at={updated}" if updated else ""
    print(f"  - {code}: status={status} opened_qty={oq} opened_price={op}{extra} runtime_log={'yes' if runtime_log.is_file() else 'no'}")
if len(enabled) > 8:
    print(f"  ... and {len(enabled) - 8} more")
PY
}

telegram_state() {
  if [ -f "$TG_CONFIG" ]; then
    echo "telegram_config: present ($TG_CONFIG)"
  else
    echo "telegram_config: missing ($TG_CONFIG)"
  fi
  if [ -f "$TG_SECRETS" ]; then
    echo "telegram_secrets: present ($TG_SECRETS)"
  else
    echo "telegram_secrets: missing ($TG_SECRETS)"
  fi
}

last_runtime_log_line() {
  if ! command -v python3 >/dev/null 2>&1 || [ ! -f "$CONFIG_PATH" ]; then
    echo "last_runtime_log: (unknown)"
    return
  fi
  MOOMOO_CPP_ROOT="$ROOT" python3 - <<PY
import json, os
from pathlib import Path

root = Path(os.environ.get("MOOMOO_CPP_ROOT", ".")).resolve()
cfg_path = Path(r"""$CONFIG_PATH""")
if not cfg_path.is_absolute():
    cfg_path = (Path.cwd() / cfg_path).resolve()
cfg = json.loads(cfg_path.read_text(encoding="utf-8"))
csv_raw = str((cfg.get("storage") or {}).get("csv_dir") or cfg.get("csv_dir") or "./strategy/daily_logs")
csv_dir = Path(csv_raw) if csv_raw.startswith("/") else (root / csv_raw).resolve()
acc = cfg.get("account") or {}
acc_id = acc.get("acc_id", 0)
acc_index = acc.get("acc_index", 0)
latest = None
for s in cfg.get("symbols") or []:
    if not s.get("enabled", True):
        continue
    code = s.get("code", "")
    tag = code.replace(".", "_")
    p = csv_dir / f"acc_{acc_id}_idx_{acc_index}" / tag / "runtime.log"
    if not p.is_file():
        continue
    try:
        line = p.read_text(encoding="utf-8", errors="replace").strip().splitlines()[-1]
    except Exception:
        continue
    if latest is None or line > latest[0]:
        latest = (line, code, p)
if latest is None:
    print("last_runtime_log: (none)")
else:
    print(f"last_runtime_log({latest[1]}): {latest[0]}")
PY
}

last_event_line() {
  if ! command -v python3 >/dev/null 2>&1 || [ ! -f "$CONFIG_PATH" ]; then
    echo "last_event: (unknown)"
    return
  fi
  MOOMOO_CPP_ROOT="$ROOT" python3 - <<PY
import json, os
from pathlib import Path

root = Path(os.environ.get("MOOMOO_CPP_ROOT", ".")).resolve()
cfg_path = Path(r"""$CONFIG_PATH""")
if not cfg_path.is_absolute():
    cfg_path = (Path.cwd() / cfg_path).resolve()
cfg = json.loads(cfg_path.read_text(encoding="utf-8"))
csv_raw = str((cfg.get("storage") or {}).get("csv_dir") or cfg.get("csv_dir") or "./strategy/daily_logs")
csv_dir = Path(csv_raw) if csv_raw.startswith("/") else (root / csv_raw).resolve()
acc = cfg.get("account") or {}
acc_id = acc.get("acc_id", 0)
acc_index = acc.get("acc_index", 0)
latest = None
for s in cfg.get("symbols") or []:
    if not s.get("enabled", True):
        continue
    code = s.get("code", "")
    tag = code.replace(".", "_")
    p = csv_dir / f"acc_{acc_id}_idx_{acc_index}" / tag / "events.csv"
    if not p.is_file():
        continue
    lines = [ln for ln in p.read_text(encoding="utf-8", errors="replace").splitlines() if ln.strip()]
    if len(lines) <= 1:
        continue
    line = lines[-1]
    if latest is None or line > latest[0]:
        latest = (line, code)
if latest is None:
    print("last_event: (none)")
else:
    print(f"last_event({latest[1]}): {latest[0]}")
PY
}

echo "=== moomoo_CPP monitor ==="
echo "time: $(date '+%F %T %Z')"
strategy_process_state
proc_state "task_worker_cpp" "$WORKER_PID_FILE"
proc_state "trade_executor_cpp" "$EXECUTOR_PID_FILE"
echo
read_config_summary
echo
telegram_state
strategy_cmdline_info
last_runtime_log_line
last_event_line
strategy_screen_info

if [ "$VERBOSE" -eq 1 ]; then
  echo
  echo "strategy_pid_file: $STRATEGY_PID_FILE"
  echo "worker_pid_file: $WORKER_PID_FILE"
  echo "executor_pid_file: $EXECUTOR_PID_FILE"
  echo "pid_root: $PID_ROOT"
  echo "binary: $ROOT/build/apps/strategy_cpp"
  echo
  echo "=== related screen sessions ==="
  if command -v screen >/dev/null 2>&1; then
    if screen -ls 2>/dev/null | grep -Ei "strategy_cpp|moomoo_cpp|trade_exec|task_worker" >/dev/null; then
      screen -ls 2>/dev/null | grep -Ei "strategy_cpp|moomoo_cpp|trade_exec|task_worker"
    else
      echo "(none matched: strategy_cpp|moomoo_cpp|trade_exec|task_worker)"
    fi
  else
    echo "screen command not found"
  fi
  echo
  echo "=== tail latest runtime.log ==="
  if command -v python3 >/dev/null 2>&1 && [ -f "$CONFIG_PATH" ]; then
    MOOMOO_CPP_ROOT="$ROOT" python3 - <<PY
import json, os
from pathlib import Path

root = Path(os.environ.get("MOOMOO_CPP_ROOT", ".")).resolve()
cfg_path = Path(r"""$CONFIG_PATH""")
if not cfg_path.is_absolute():
    cfg_path = (Path.cwd() / cfg_path).resolve()
cfg = json.loads(cfg_path.read_text(encoding="utf-8"))
csv_raw = str((cfg.get("storage") or {}).get("csv_dir") or cfg.get("csv_dir") or "./strategy/daily_logs")
csv_dir = Path(csv_raw) if csv_raw.startswith("/") else (root / csv_raw).resolve()
acc = cfg.get("account") or {}
acc_id = acc.get("acc_id", 0)
acc_index = acc.get("acc_index", 0)
best = None
for s in cfg.get("symbols") or []:
    if not s.get("enabled", True):
        continue
    code = s.get("code", "")
    tag = code.replace(".", "_")
    p = csv_dir / f"acc_{acc_id}_idx_{acc_index}" / tag / "runtime.log"
    if p.is_file():
        mtime = p.stat().st_mtime
        if best is None or mtime > best[0]:
            best = (mtime, p, code)
if best is None:
    print("(no runtime.log found)")
else:
    print(f"# {best[2]} -> {best[1]}")
    lines = best[1].read_text(encoding="utf-8", errors="replace").splitlines()
    for ln in lines[-20:]:
        print(ln)
PY
  else
    echo "(python3 or config missing)"
  fi
fi
