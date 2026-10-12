#!/usr/bin/env bash
# C++ 策略启动（对齐 moomoo_python/run_strategy_real.sh，paper/real 由 config JSON 的 mode 决定）
# 用法:
#   ./run_strategy.sh -c strategy/config/strategy_config_us.json
#   ./run_strategy.sh -us
#   ./run_strategy.sh -hk
# 其余参数原样传给 strategy_cpp（如 --max-ticks 20 --poll-sec 5 --dry-run）
# 可选:
#   SKIP_BUILD=1 ./run_strategy.sh -c path/to/config.json
#   ENV_FILE=../moomoo_python/.env.real ./run_strategy.sh -c ...

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
PY_ROOT="$(cd "${ROOT}/../moomoo_python" && pwd)"
BUILD="${ROOT}/build"
SDK="${MOOMOO_SDK_ROOT:-/home/ubuntu/workspace/MMAPI4CPP_10.9.6908}"
BIN="${BUILD}/apps/strategy_cpp"
ENV_FILE="${ENV_FILE:-${PY_ROOT}/.env.real}"

export MOOMOO_PYTHON_STRATEGY="${MOOMOO_PYTHON_STRATEGY:-${PY_ROOT}/strategy}"
export MOOMOO_CPP_ROOT="${ROOT}"
export MOOMOO_CPP_STRATEGY="${MOOMOO_CPP_STRATEGY:-${ROOT}/strategy}"
export MOOMOO_PYTHON_ROOT="${PY_ROOT}"

if [ -f "$ENV_FILE" ]; then
  set -a
  # shellcheck disable=SC1090
  source "$ENV_FILE"
  set +a
fi

read_config_meta() {
  MOOMOO_PYTHON_STRATEGY="${MOOMOO_PYTHON_STRATEGY}" python3 - "$@" <<'PY'
import json, os, sys
from pathlib import Path

argv = sys.argv[1:]
cpp_strategy = Path(os.environ.get("MOOMOO_CPP_STRATEGY", "")).resolve()
py_strategy = Path(os.environ.get("MOOMOO_PYTHON_STRATEGY", "../moomoo_python/strategy")).resolve()
strategy_dir = cpp_strategy if cpp_strategy.is_dir() else py_strategy

def abs_path(raw: str) -> Path:
    p = Path(raw)
    return p if p.is_absolute() else (Path.cwd() / p).resolve()

config_path = ""
preset_hk = preset_us = False
i = 0
while i < len(argv):
    a = argv[i]
    if a in ("-hk", "--hk"):
        preset_hk = True
    elif a in ("-us", "--us"):
        preset_us = True
    elif a in ("-c", "--config") and i + 1 < len(argv):
        i += 1
        config_path = argv[i]
    elif not a.startswith("-") and not config_path:
        config_path = a
    i += 1

if preset_hk:
    path = strategy_dir / "config/strategy_config_hk.json"
elif preset_us:
    path = strategy_dir / "config/strategy_config_us.json"
else:
    raw = config_path or os.environ.get("STRATEGY_CONFIG") or os.environ.get("STRATEGY_CONFIG_PATH") or ""
    if not raw:
        print("CONFIG_PATH=\nMODE=\n")
        sys.exit(0)
    path = abs_path(raw)

if not path.is_file():
    print(f"CONFIG_PATH={path}\nMODE=\n")
    sys.exit(0)

cfg = json.loads(path.read_text(encoding="utf-8"))
mode = str(cfg.get("mode", "paper")).strip().lower()
print(f"CONFIG_PATH={path}\nMODE={mode}\n")
PY
}

META="$(read_config_meta "$@")"
CONFIG_PATH="$(printf '%s\n' "$META" | sed -n 's/^CONFIG_PATH=//p' | head -1)"
CONFIG_MODE="$(printf '%s\n' "$META" | sed -n 's/^MODE=//p' | head -1)"

if [ "${CONFIG_MODE}" = "real" ]; then
  MOOMOO_TRADE_FERNET_KEY_FILE="${MOOMOO_TRADE_FERNET_KEY_FILE:-${PY_ROOT}/secrets/moomoo_fernet.key}"
  MOOMOO_TRADE_PWD_ENC_FILE="${MOOMOO_TRADE_PWD_ENC_FILE:-${PY_ROOT}/secrets/moomoo_trade_pwd.fernet}"
  if [ "${MOOMOO_TRADE_FERNET_KEY_FILE:-}" = "" ] || [ "${MOOMOO_TRADE_PWD_ENC_FILE:-}" = "" ]; then
    echo "[ERROR] config mode=real 缺少环境变量：" >&2
    echo "  MOOMOO_TRADE_FERNET_KEY_FILE" >&2
    echo "  MOOMOO_TRADE_PWD_ENC_FILE" >&2
    echo "请在 ${ENV_FILE} 中配置，或先 export 后再运行。" >&2
    exit 2
  fi
  if [ ! -f "$MOOMOO_TRADE_FERNET_KEY_FILE" ]; then
    echo "[ERROR] 密钥文件不存在: $MOOMOO_TRADE_FERNET_KEY_FILE" >&2
    exit 2
  fi
  if [ ! -f "$MOOMOO_TRADE_PWD_ENC_FILE" ]; then
    echo "[ERROR] 密文文件不存在: $MOOMOO_TRADE_PWD_ENC_FILE" >&2
    exit 2
  fi
fi

if [ ! -f "${SDK}/Include/Include_MMAPI.h" ]; then
  echo "[ERROR] SDK not found: ${SDK}" >&2
  echo "  set MOOMOO_SDK_ROOT if installed elsewhere" >&2
  exit 1
fi

if [ "${SKIP_BUILD:-0}" != "1" ]; then
  cmake -S "${ROOT}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=ON -DBUILD_TRADE=ON -DBUILD_BENCHES=OFF >/dev/null
  cmake --build "${BUILD}" -j "$(nproc)" --target strategy_cpp
fi

if [ ! -x "${BIN}" ]; then
  echo "[ERROR] strategy_cpp not built: ${BIN}" >&2
  exit 1
fi

export LD_LIBRARY_PATH="${SDK}/Bin/Centos7:${LD_LIBRARY_PATH:-}"

echo "[RUN] strategy_cpp start"
if [ "$#" -gt 0 ]; then
  echo "  args   : $*"
fi
echo "  binary : ${BIN}"
echo "  sdk    : ${SDK}"
echo "  strategy_dir : ${MOOMOO_CPP_STRATEGY}"
if [ -n "${CONFIG_PATH}" ]; then
  echo "  config : ${CONFIG_PATH}"
fi
if [ -n "${CONFIG_MODE}" ]; then
  echo "  mode   : ${CONFIG_MODE} (from JSON)"
fi
if [ "${ENTRY_NET_SPLIT_ENABLED:-}" = "1" ] || [ "${ENTRY_NET_SPLIT_ENABLED:-}" = "true" ]; then
  export CPP_NET_SPLIT_ENABLED="${CPP_NET_SPLIT_ENABLED:-1}"
  echo "  net_split: ENTRY_NET_SPLIT_ENABLED=1 CPP_NET_SPLIT_ENABLED=${CPP_NET_SPLIT_ENABLED}"
fi
echo "  logs   : runtime.log under ./strategy/daily_logs/<acc>/<symbol>/ (tail -f to watch)"
if [ "${CONFIG_MODE}" = "real" ]; then
  echo "  env    : ${ENV_FILE##${PY_ROOT}/}"
  echo "  key    : ${MOOMOO_TRADE_FERNET_KEY_FILE}"
  echo "  enc    : ${MOOMOO_TRADE_PWD_ENC_FILE}"
fi
echo

exec "${BIN}" "$@"
