cmake_minimum_required(VERSION 3.16)
project(moomoo_cpp_bench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# SDK 静态库 libMMAPI.a 非 PIE，Ubuntu 默认 PIE 链接会失败
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -no-pie")
# SDK Linux 库使用旧 libstdc++ ABI，否则 protobuf string 符号会出现 abi:cxx11 链接错误
add_compile_definitions(_GLIBCXX_USE_CXX11_ABI=0)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

set(SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../MMAPI4CPP_10.9.6908" CACHE PATH "MMAPI4CPP SDK")
set(SDK_INCLUDE "${SDK_ROOT}/Include")
set(SDK_SAMPLE "${SDK_ROOT}/Sample/SimpleSample")
set(SDK_LIB_DIR "${SDK_ROOT}/Bin/Centos7" CACHE PATH "Linux SDK libs")

set(BENCH_TARGETS "all" CACHE STRING "Build targets: kline, order, or all")

if(NOT EXISTS "${SDK_INCLUDE}/Include_MMAPI.h")
  message(FATAL_ERROR "SDK not found: ${SDK_INCLUDE}")
endif()

function(mm_apply_sdk_includes target)
  target_include_directories(${target} PRIVATE
    "${SDK_INCLUDE}"
    "${SDK_SAMPLE}"
  )
endfunction()

add_library(mm_demo_base STATIC "${SDK_SAMPLE}/FTAPIDemoBase.cpp")
mm_apply_sdk_includes(mm_demo_base)

set(MM_BENCH_LIBS
  mm_demo_base
  "${SDK_LIB_DIR}/libMMAPI.a"
  "${SDK_LIB_DIR}/libAPIChannel.so"
  "${SDK_LIB_DIR}/libprotobuf.a"
  z pthread ${CMAKE_DL_LIBS}
)

function(add_mm_bench target src)
  add_executable(${target} ${src})
  mm_apply_sdk_includes(${target})
  target_link_libraries(${target} PRIVATE ${MM_BENCH_LIBS})
  set_target_properties(${target} PROPERTIES
    BUILD_RPATH "${SDK_LIB_DIR}"
    INSTALL_RPATH "${SDK_LIB_DIR}"
  )
endfunction()

if(BENCH_TARGETS STREQUAL "all" OR BENCH_TARGETS STREQUAL "kline")
  add_mm_bench(bench_kline bench_kline.cpp)
endif()
if(BENCH_TARGETS STREQUAL "all" OR BENCH_TARGETS STREQUAL "push")
  add_mm_bench(bench_push bench_push.cpp)
  add_mm_bench(bench_push_batch bench_push_batch.cpp)
endif()
if(BENCH_TARGETS STREQUAL "all" OR BENCH_TARGETS STREQUAL "order")
  add_mm_bench(bench_order bench_order.cpp)
  add_mm_bench(bench_real_roundtrip bench_real_roundtrip.cpp)
  add_mm_bench(bench_query_cancel bench_query_cancel.cpp)
endif()
