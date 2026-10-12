#!/usr/bin/env bash
# L0 单元测试：离线，无需 OpenD。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build"

if ! command -v cmake >/dev/null 2>&1; then
  echo "[run_tests_l0] ERROR: cmake not installed" >&2
  exit 1
fi

cmake -S "${ROOT}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DBUILD_BENCHES=OFF
cmake --build "${BUILD}" -j "$(nproc)" --target l0_tests

echo "[run_tests_l0] running ctest..."
ctest --test-dir "${BUILD}" --output-on-failure

echo "[run_tests_l0] ok (${BUILD}/tests/l0_tests)"
