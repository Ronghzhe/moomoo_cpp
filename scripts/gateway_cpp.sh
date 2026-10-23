#!/usr/bin/env bash
# Start/stop C++ OpenD place gateway (default :18766).
# Independent from Python gateway (:18765).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PY_ROOT="${MOOMOO_PYTHON_ROOT:-/home/ubuntu/workspace/moomoo_python}"
ENV_FILE="${MOOMOO_TRADE_ENV_FILE:-$PY_ROOT/.env.real}"
apply_trade_env() {
  if [[ -f "$ENV_FILE" ]]; then
    set -a
    # shellcheck disable=SC1090
    source "$ENV_FILE"
    set +a
  fi
  export MOOMOO_TRADE_FERNET_KEY_FILE="${MOOMOO_TRADE_FERNET_KEY_FILE:-$PY_ROOT/secrets/moomoo_fernet.key}"
  export MOOMOO_TRADE_PWD_ENC_FILE="${MOOMOO_TRADE_PWD_ENC_FILE:-$PY_ROOT/secrets/moomoo_trade_pwd.fernet}"
}
BIN="${GATEWAY_CPP_BIN:-$ROOT/build/apps/opend_gateway_cpp}"
PID_FILE="${GATEWAY_CPP_PID_FILE:-$ROOT/sys_log/opend_gateway_cpp.pid}"
LOG_DIR="${GATEWAY_CPP_LOG_DIR:-$ROOT/sys_log/gateway_cpp}"
LOG_FILE="${GATEWAY_CPP_LOG_FILE:-$LOG_DIR/gateway.log}"
BIND_HOST="${GATEWAY_CPP_BIND_HOST:-127.0.0.1}"
BIND_PORT="${GATEWAY_CPP_BIND_PORT:-18766}"
OPEND_HOST="${GATEWAY_CPP_OPEND_HOST:-127.0.0.1}"
OPEND_PORT="${GATEWAY_CPP_OPEND_PORT:-11121}"
HEALTH_URL="http://${BIND_HOST}:${BIND_PORT}/health"
MODE_FLAG="${GATEWAY_CPP_MODE:---live}"

mkdir -p "$(dirname "$PID_FILE")" "$LOG_DIR"

is_running() {
  if [[ -f "$PID_FILE" ]]; then
    local pid
    pid="$(tr -d ' \t\r\n' <"$PID_FILE" || true)"
    if [[ -n "${pid:-}" ]] && kill -0 "$pid" 2>/dev/null; then
      return 0
    fi
  fi
  return 1
}

gateway_health_ok() {
  curl -fsS --max-time 2 "$HEALTH_URL" >/dev/null 2>&1
}

start() {
  if is_running; then
    echo "[opend-gateway-cpp] already running, pid=$(tr -d ' \t\r\n' <"$PID_FILE")"
    return 0
  fi
  if [[ ! -x "$BIN" ]]; then
    echo "[opend-gateway-cpp] binary not found: $BIN (build with BUILD_TRADE=ON)" >&2
    return 1
  fi
  apply_trade_env
  echo "[opend-gateway-cpp] starting bind=${BIND_HOST}:${BIND_PORT} mode=${MODE_FLAG} ..."
  nohup "$BIN" \
    --bind-host "$BIND_HOST" \
    --bind-port "$BIND_PORT" \
    --opend-host "$OPEND_HOST" \
    --opend-port "$OPEND_PORT" \
    --log-dir "$LOG_DIR" \
    $MODE_FLAG \
    >>"$LOG_FILE" 2>&1 &
  echo $! >"$PID_FILE"
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    if is_running && gateway_health_ok; then
      echo "[opend-gateway-cpp] started pid=$(tr -d ' \t\r\n' <"$PID_FILE") health=ok"
      echo "[opend-gateway-cpp] log: $LOG_FILE"
      return 0
    fi
    sleep 0.3
  done
  if is_running; then
    echo "[opend-gateway-cpp] started pid=$(tr -d ' \t\r\n' <"$PID_FILE") (health check pending)"
    return 0
  fi
  echo "[opend-gateway-cpp] failed to start; see $LOG_FILE" >&2
  return 1
}

stop() {
  if ! is_running; then
    echo "[opend-gateway-cpp] not running"
    rm -f "$PID_FILE"
    return 0
  fi
  local pid
  pid="$(tr -d ' \t\r\n' <"$PID_FILE")"
  echo "[opend-gateway-cpp] stopping pid=$pid ..."
  kill "$pid" 2>/dev/null || true
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    if ! kill -0 "$pid" 2>/dev/null; then
      break
    fi
    sleep 0.2
  done
  if kill -0 "$pid" 2>/dev/null; then
    kill -9 "$pid" 2>/dev/null || true
  fi
  rm -f "$PID_FILE"
  echo "[opend-gateway-cpp] stopped"
}

status() {
  if is_running; then
    local pid health="down"
    pid="$(tr -d ' \t\r\n' <"$PID_FILE")"
    if gateway_health_ok; then
      health="ok"
    fi
    echo "[opend-gateway-cpp] running pid=$pid health=$health url=$HEALTH_URL"
  else
    echo "[opend-gateway-cpp] not running"
  fi
}

ACTION="${1:-status}"
case "$ACTION" in
  start) start ;;
  stop) stop ;;
  status) status ;;
  restart) stop; start ;;
  *)
    echo "usage: $0 {start|stop|restart|status}" >&2
    exit 2
    ;;
esac
