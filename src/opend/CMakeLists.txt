set(SDK_ROOT "${CMAKE_SOURCE_DIR}/../MMAPI4CPP_10.9.6908" CACHE PATH "MMAPI4CPP SDK")
set(SDK_INCLUDE "${SDK_ROOT}/Include")
set(SDK_SAMPLE "${SDK_ROOT}/Sample/SimpleSample")
set(SDK_LIB_DIR "${SDK_ROOT}/Bin/Centos7" CACHE PATH "Linux SDK libs")

if(NOT EXISTS "${SDK_INCLUDE}/Include_MMAPI.h")
  message(FATAL_ERROR "SDK not found: ${SDK_INCLUDE}")
endif()

add_library(mm_demo_base STATIC "${SDK_SAMPLE}/FTAPIDemoBase.cpp")
target_include_directories(mm_demo_base PRIVATE "${SDK_INCLUDE}" "${SDK_SAMPLE}")
target_compile_definitions(mm_demo_base PRIVATE _GLIBCXX_USE_CXX11_ABI=0)

add_library(moomoo_trade STATIC
  trade_connection.cpp
  order_client.cpp
  account_client.cpp
  kline_connection.cpp
  kline_service.cpp
  quote_connection.cpp
  connect_guard.cpp
  direct_broker.cpp
  broker_factory.cpp
  ../gateway/gateway_runtime.cpp
  ../gateway/http_server.cpp
  ../strategy/order_pending_loop.cpp
  ../strategy/net_split_broker_query.cpp
  ../strategy/net_split_executor.cpp
  ../strategy/live_eval_compute.cpp
  live_eval_inject.cpp
)

target_include_directories(moomoo_trade PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
  "${SDK_INCLUDE}"
  "${SDK_SAMPLE}"
)
target_link_libraries(moomoo_trade PUBLIC
  moomoo_core
  mm_demo_base
  "${SDK_LIB_DIR}/libMMAPI.a"
  "${SDK_LIB_DIR}/libAPIChannel.so"
  "${SDK_LIB_DIR}/libprotobuf.a"
  z pthread ${CMAKE_DL_LIBS}
)

set(MOOMOO_SDK_RPATH "${SDK_LIB_DIR}")
