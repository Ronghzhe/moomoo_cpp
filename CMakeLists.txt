cmake_minimum_required(VERSION 3.16)
project(moomoo_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Match MMAPI SDK libstdc++ ABI (see cpp/CMakeLists.txt)
add_compile_definitions(_GLIBCXX_USE_CXX11_ABI=0)

option(BUILD_TESTS "Build L0 unit tests" ON)
option(BUILD_TRADE "Build OpenD trade library and L1 contract app" OFF)
option(BUILD_BENCHES "Build OpenD SDK benchmarks (cpp/)" OFF)

add_subdirectory(src)

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(BUILD_TRADE)
  add_subdirectory(apps)
endif()

if(BUILD_BENCHES)
  add_subdirectory(cpp)
endif()
