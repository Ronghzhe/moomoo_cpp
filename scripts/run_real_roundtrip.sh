#!/usr/bin/env bash
# REAL round-trip benchmark for HK.02208 (buy 200 -> sell 200).
#
# Usage:
#   ./scripts/run_real_roundtrip.sh
#   ./scripts/run_real_roundtrip.sh --skip-python   # only run C++ (resume)
#   ./scripts/run_real_roundtrip.sh --skip-cpp       # only run Python
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PY="/home/ubuntu/workspace/venv/bin/python"
CFG="${ROOT}/config/bench_real.yaml"
OUT_DIR="${ROOT}/reports/opend_bench_real_$(date +%Y%m%d)"
SKIP_PYTHON=0
SKIP_CPP=0

while [ $# -gt 0 ]; do
  case "$1" in
    --skip-python) SKIP_PYTHON=1 ;;
    --skip-cpp) SKIP_CPP=1 ;;
    -h|--help)
      sed -n '1,8p' "$0"
      exit 0
      ;;
    *)
      echo "[real] unknown arg: $1" >&2
      exit 2
      ;;
  esac
  shift
done

mkdir -p "${OUT_DIR}"

ENV_REAL="/home/ubuntu/workspace/moomoo_python/.env.real"
if [ -f "${ENV_REAL}" ]; then
  set -a
  # shellcheck disable=SC1090
  source "${ENV_REAL}"
  set +a
fi

export LD_LIBRARY_PATH="/home/ubuntu/workspace/MMAPI4CPP_10.9.6908/Bin/Centos7:${LD_LIBRARY_PATH:-}"

read -r LANG_GAP ORDER_GAP QUERY_GAP ROUNDTRIP_GAP RL_MAX RL_WAIT <<< "$("${PY}" - <<PY
import sys
sys.path.insert(0, "${ROOT}/python")
from bench_common import load_config
from pathlib import Path
cfg = load_config(Path("${CFG}"))
print(
    int(cfg.get("lang_gap_sec") or 120),
    float(cfg.get("order_gap_sec") or 2.5),
    float(cfg.get("query_gap_sec") or 1.0),
    float(cfg.get("roundtrip_gap_sec") or 3.0),
    int(cfg.get("rate_limit_retry_max") or 5),
    float(cfg.get("rate_limit_retry_wait_sec") or 2.5),
)
PY
)"

echo "[real] output dir: ${OUT_DIR}"
echo "[real] rate_limit: order_gap=${ORDER_GAP}s query_gap=${QUERY_GAP}s lang_gap=${LANG_GAP}s retry_max=${RL_MAX}"

read -r _ BUY_PX SELL_PX PWD_MD5 <<< "$("${PY}" "${ROOT}/python/print_real_prices.py" HK.02208 127.0.0.1 11121 2>/dev/null | grep '^PRICES' | tail -1)"

echo "[real] snapshot buy=${BUY_PX} sell=${SELL_PX}"

PY_OUT="${OUT_DIR}/real_py_order_02208.jsonl"
CPP_OUT="${OUT_DIR}/real_cpp_order_02208.jsonl"

if [ "${SKIP_PYTHON}" -eq 0 ]; then
  if [ -f "${PY_OUT}" ] && grep -q '"ok": true' "${PY_OUT}" 2>/dev/null; then
    echo "[real] Python result already ok, skip (--skip-python to force rerun)"
  else
    echo "[real] Python roundtrip..."
    "${PY}" "${ROOT}/python/bench_real_roundtrip.py" \
      --config "${CFG}" \
      --out "${PY_OUT}" \
      --lang python_direct
  fi
else
  echo "[real] skip Python"
fi

if [ "${SKIP_CPP}" -eq 0 ]; then
  if [ -f "${CPP_OUT}" ] && grep -q '"ok": true' "${CPP_OUT}" 2>/dev/null; then
    echo "[real] C++ result already ok, skip"
  else
    echo "[real] wait ${LANG_GAP}s before C++ (OpenD rate limit cooldown)..."
    sleep "${LANG_GAP}"

    CPP_BIN="${ROOT}/cpp/build/bench_real_roundtrip"
    if [ ! -x "${CPP_BIN}" ]; then
      echo "[real] building bench_real_roundtrip..."
      cd "${ROOT}/cpp/build" && cmake --build . -j "$(nproc)" --target bench_real_roundtrip
    fi

    echo "[real] C++ roundtrip..."
    "${CPP_BIN}" \
      --host 127.0.0.1 --port 11121 \
      --code HK.02208 --qty 200 \
      --buy-price "${BUY_PX}" --sell-price "${SELL_PX}" \
      --pwd-md5 "${PWD_MD5}" \
      --order-gap-sec "${ORDER_GAP}" \
      --query-gap-sec "${QUERY_GAP}" \
      --roundtrip-gap-sec "${ROUNDTRIP_GAP}" \
      --rate-limit-retry-max "${RL_MAX}" \
      --rate-limit-retry-wait-sec "${RL_WAIT}" \
      --out "${CPP_OUT}"
  fi
else
  echo "[real] skip C++"
fi

echo "[real] done"
echo "  python: ${PY_OUT}"
echo "  cpp:    ${CPP_OUT}"
