#!/usr/bin/env bash
# L2 Golden parity: replay fixtures vs Python-generated golden outputs.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build"
PY="${PYTHON:-/home/ubuntu/workspace/venv/bin/python}"

echo "[run_parity_l2] generating golden from Python..."
"${PY}" "${ROOT}/scripts/generate_l2_golden.py"

if ! command -v cmake >/dev/null 2>&1; then
  echo "[run_parity_l2] ERROR: cmake not installed" >&2
  exit 1
fi

cmake -S "${ROOT}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DBUILD_BENCHES=OFF
cmake --build "${BUILD}" -j "$(nproc)" --target l0_tests

echo "[run_parity_l2] running L2GoldenReplay..."
cd "${BUILD}"
./tests/l0_tests --gtest_filter='L2GoldenReplay.*'

echo "[run_parity_l2] ok"
