#!/usr/bin/env bash
# OpenD 延迟 benchmark（模拟盘 SIMULATE）
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
PY="${PYTHON:-/home/ubuntu/workspace/venv/bin/python}"
CFG="${ROOT}/config/bench.yaml"
DATE_TAG="$(date +%Y%m%d_%H%M%S)"
REPORT_DIR="${ROOT}/reports/opend_bench_${DATE_TAG}"
RAW_DIR="${REPORT_DIR}/raw"
CPP_BUILD="${ROOT}/cpp/build"
GATEWAY_SH="/home/ubuntu/workspace/moomoo_python/strategy/task_system/script/gateway.sh"

KLINE_ONLY=0
ORDER_ONLY=0
SKIP_BUILD=0
SKIP_CPP=0

while [ $# -gt 0 ]; do
  case "$1" in
    --kline-only) KLINE_ONLY=1 ;;
    --order-only) ORDER_ONLY=1 ;;
    --skip-build) SKIP_BUILD=1 ;;
    --skip-cpp) SKIP_CPP=1 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
  shift
done

read_cfg() {
  "$PY" - <<PY
import sys
sys.path.insert(0, "${ROOT}/python")
from bench_common import load_config
from pathlib import Path
cfg = load_config(Path("${CFG}"))
print(cfg.get("$1", ""))
PY
}

TRD_ENV="$(read_cfg trd_env)"
if [ "${TRD_ENV}" != "SIMULATE" ]; then
  echo "[bench] ERROR: config trd_env must be SIMULATE, got ${TRD_ENV}" >&2
  exit 2
fi

HOST="$(read_cfg opend_host)"
PORT="$(read_cfg opend_port)"
CODE="$(read_cfg code)"
BARS="$(read_cfg bars)"
WARM="$(read_cfg warm_iters)"
COLD="$(read_cfg cold_iters)"
ORDER_ITERS="$(read_cfg order_iters)"
QTY="$(read_cfg order_qty)"
PRICE="$(read_cfg order_price)"
GW_URL="$(read_cfg gateway_url)"

mkdir -p "${RAW_DIR}"

echo "[bench] SIMULATE mode | report=${REPORT_DIR}"

if [ "${SKIP_CPP}" -eq 0 ]; then
  if [ "${SKIP_BUILD}" -eq 0 ]; then
    echo "[bench] building C++ ..."
    if ! cmake -S "${ROOT}/cpp" -B "${CPP_BUILD}" -DCMAKE_BUILD_TYPE=Release; then
      echo "[bench] WARN: cmake configure failed; use --skip-cpp or see README C++ 编译说明" >&2
      SKIP_CPP=1
    elif ! cmake --build "${CPP_BUILD}" -j "$(nproc)"; then
      echo "[bench] WARN: C++ build failed; continuing Python-only (--skip-cpp)" >&2
      SKIP_CPP=1
    fi
  fi
fi

export LD_LIBRARY_PATH="/home/ubuntu/workspace/MMAPI4CPP_10.9.6908/Bin/Centos7:${LD_LIBRARY_PATH:-}"

run_kline() {
  echo "[bench] === K line K_5M ==="
  OUT_PY="${RAW_DIR}/python_kline.jsonl"
  OUT_CPP="${RAW_DIR}/cpp_kline.jsonl"
  rm -f "${OUT_PY}" "${OUT_CPP}"
  "$PY" "${ROOT}/python/bench_kline.py" --config "${CFG}" --out "${OUT_PY}" --mode both
  if [ "${SKIP_CPP}" -eq 0 ] && [ -x "${CPP_BUILD}/bench_kline" ]; then
    "${CPP_BUILD}/bench_kline" \
      --host "${HOST}" --port "${PORT}" --symbol "${CODE}" --bars "${BARS}" \
      --warm-iters "${WARM}" --cold-iters "${COLD}" --mode both --out "${OUT_CPP}"
  else
    echo "[bench] skip cpp kline (binary missing or --skip-cpp)"
  fi
}

run_order() {
  echo "[bench] === Order (direct + gateway) ==="
  if [ -x "${GATEWAY_SH}" ]; then
    "${GATEWAY_SH}" start || true
    sleep 0.5
  fi
  OUT_PY="${RAW_DIR}/python_order_direct.jsonl"
  OUT_GW="${RAW_DIR}/python_order_gateway.jsonl"
  OUT_CPP="${RAW_DIR}/cpp_order.jsonl"
  rm -f "${OUT_PY}" "${OUT_GW}" "${OUT_CPP}"
  "$PY" "${ROOT}/python/bench_order_direct.py" --config "${CFG}" --out "${OUT_PY}" --mode both
  "$PY" "${ROOT}/python/bench_order_gateway.py" --config "${CFG}" --out "${OUT_GW}" || {
    echo "[bench] WARN: gateway bench failed (is gateway running at ${GW_URL}?)" >&2
  }
  if [ "${SKIP_CPP}" -eq 0 ] && [ -x "${CPP_BUILD}/bench_order" ]; then
    "${CPP_BUILD}/bench_order" \
      --host "${HOST}" --port "${PORT}" --code "${CODE}" \
      --qty "${QTY}" --price "${PRICE}" \
      --warm-iters "${ORDER_ITERS}" --cold-iters "${COLD}" --mode both --out "${OUT_CPP}"
  else
    echo "[bench] skip cpp order (binary missing or --skip-cpp)"
  fi
}

if [ "${ORDER_ONLY}" -eq 0 ]; then
  run_kline
fi
if [ "${KLINE_ONLY}" -eq 0 ]; then
  run_order
fi

"$PY" "${ROOT}/report/generate_report.py" \
  --report-dir "${REPORT_DIR}" \
  --symbol "${CODE}" \
  --kl-type "K_5M" \
  --bars "${BARS}"

echo "[bench] done"
echo "  ${REPORT_DIR}/summary.md"
echo "  ${REPORT_DIR}/summary.csv"
