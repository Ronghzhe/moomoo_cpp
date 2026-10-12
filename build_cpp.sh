#!/usr/bin/env bash
# 编译 C++ benchmark（依赖 MMAPI4CPP_10.9.6908 Linux 库）
#
# 用法:
#   ./build_cpp.sh              # 编 bench_kline + bench_order
#   ./build_cpp.sh kline        # 只编 K 线（快速实验推荐）
#   ./build_cpp.sh push         # 只编 BasicQot 推送测试
#   ./build_cpp.sh order        # 只编报单
#   ./build_cpp.sh kline --fast # 跳过 cmake configure（改 CMakeLists 后勿用）
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
SDK="/home/ubuntu/workspace/MMAPI4CPP_10.9.6908"
BUILD="${ROOT}/cpp/build"
TARGETS="all"
FAST=0

while [ $# -gt 0 ]; do
  case "$1" in
    kline|push|order|all) TARGETS="$1" ;;
    --fast) FAST=1 ;;
    -h|--help)
      sed -n '2,8p' "$0"
      exit 0
      ;;
    *)
      echo "[build_cpp] unknown arg: $1" >&2
      exit 2
      ;;
  esac
  shift
done

if [ ! -f "${SDK}/Include/Include_MMAPI.h" ]; then
  echo "[build_cpp] ERROR: SDK not found at ${SDK}" >&2
  exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
  echo "[build_cpp] ERROR: cmake not installed (apt install cmake g++)" >&2
  exit 1
fi

export LD_LIBRARY_PATH="${SDK}/Bin/Centos7:${LD_LIBRARY_PATH:-}"

CMAKE_ARGS=(-S "${ROOT}/cpp" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release -DBENCH_TARGETS="${TARGETS}")

if [ "${FAST}" -eq 0 ] || [ ! -f "${BUILD}/Makefile" ]; then
  cmake "${CMAKE_ARGS[@]}"
fi

case "${TARGETS}" in
  kline) BUILD_TARGET=bench_kline ;;
  push) BUILD_TARGET="bench_push bench_push_batch" ;;
  order) BUILD_TARGET=bench_order ;;
  all)   BUILD_TARGET="" ;;
esac

if [ -n "${BUILD_TARGET}" ]; then
  # shellcheck disable=SC2086
  for t in ${BUILD_TARGET}; do
    cmake --build "${BUILD}" -j "$(nproc)" --target "${t}"
    echo "[build_cpp] ok: ${BUILD}/${t}"
  done
else
  cmake --build "${BUILD}" -j "$(nproc)"
  echo "[build_cpp] ok: ${BUILD}/bench_kline ${BUILD}/bench_order"
fi
