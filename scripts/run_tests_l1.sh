#!/usr/bin/env bash
# L1 OpenD 契约测试：SIMULATE 下 account / kline / order / direct_broker。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build"
CFG="${ROOT}/config/bench.yaml"
REPORT_DIR="${ROOT}/reports/contract"
PY="${PYTHON:-python3}"
SDK="/home/ubuntu/workspace/MMAPI4CPP_10.9.6908"
TS="$(date +%Y%m%d_%H%M%S)"
OUT="${REPORT_DIR}/l1_suite_${TS}.jsonl"

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

run_case() {
  local name="$1"
  shift
  echo "[run_tests_l1] running ${name}"
  set +e
  "$@" | tee -a "${OUT}"
  local rc=${PIPESTATUS[0]}
  set -e
  if [ "${rc}" -ne 0 ]; then
    echo "[run_tests_l1] FAILED ${name} rc=${rc}" >&2
    return "${rc}"
  fi
  if ! tail -n 1 "${OUT}" | grep -q '"ok":true'; then
    echo "[run_tests_l1] FAILED ${name}: ok!=true" >&2
    return 1
  fi
  return 0
}

TRD_ENV="$(read_cfg trd_env)"
if [ "${TRD_ENV}" != "SIMULATE" ]; then
  echo "[run_tests_l1] ERROR: config trd_env must be SIMULATE, got ${TRD_ENV}" >&2
  exit 2
fi

HOST="$(read_cfg opend_host)"
PORT="$(read_cfg opend_port)"
CODE="$(read_cfg code)"
SYMBOL="$(read_cfg symbol)"
KTYPE="$(read_cfg kl_type)"
BARS="$(read_cfg bars)"
QTY="$(read_cfg order_qty)"
PRICE="$(read_cfg order_price)"

if [ -z "${CODE}" ]; then
  CODE="${SYMBOL}"
fi

if [ ! -f "${SDK}/Include/Include_MMAPI.h" ]; then
  echo "[run_tests_l1] ERROR: SDK not found at ${SDK}" >&2
  exit 1
fi

if ! (echo >/dev/tcp/"${HOST}"/"${PORT}") 2>/dev/null; then
  echo "[run_tests_l1] ERROR: OpenD not reachable at ${HOST}:${PORT}" >&2
  exit 1
fi

cmake -S "${ROOT}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTS=ON -DBUILD_TRADE=ON -DBUILD_BENCHES=OFF
cmake --build "${BUILD}" -j "$(nproc)" \
  --target l1_account l1_kline l1_contract l1_direct_broker

mkdir -p "${REPORT_DIR}"
: > "${OUT}"
export LD_LIBRARY_PATH="${SDK}/Bin/Centos7:${LD_LIBRARY_PATH:-}"

COMMON=(--host "${HOST}" --port "${PORT}" --code "${CODE}")
FAIL=0

run_case l1_account "${BUILD}/apps/l1_account" "${COMMON[@]}" || FAIL=1
run_case l1_kline "${BUILD}/apps/l1_kline" "${COMMON[@]}" --ktype "${KTYPE}" --bars "${BARS}" || FAIL=1
run_case l1_contract "${BUILD}/apps/l1_contract" "${COMMON[@]}" --qty "${QTY}" --price "${PRICE}" || FAIL=1
run_case l1_direct_broker "${BUILD}/apps/l1_direct_broker" "${COMMON[@]}" --qty "${QTY}" --price "${PRICE}" || FAIL=1

if [ "${FAIL}" -ne 0 ]; then
  echo "[run_tests_l1] suite FAILED (see ${OUT})" >&2
  exit 1
fi

echo "[run_tests_l1] suite ok -> ${OUT}"
