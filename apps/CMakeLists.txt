function(add_l1_app target)
  add_executable(${target} ${ARGN})
  target_link_libraries(${target} PRIVATE moomoo_trade)
  target_include_directories(${target} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SDK_INCLUDE}"
    "${SDK_SAMPLE}"
  )
  set_target_properties(${target} PROPERTIES
    BUILD_RPATH "${MOOMOO_SDK_RPATH}"
    INSTALL_RPATH "${MOOMOO_SDK_RPATH}"
    LINK_FLAGS "-no-pie"
  )
endfunction()

add_l1_app(l1_contract l1_contract_main.cpp)
add_l1_app(l1_account l1_account_main.cpp)
add_l1_app(l1_kline l1_kline_main.cpp)
add_l1_app(l1_direct_broker l1_direct_broker_main.cpp)
add_l1_app(opend_gateway_cpp opend_gateway_main.cpp)

add_executable(strategy_cpp strategy_cpp_main.cpp)
target_link_libraries(strategy_cpp PRIVATE moomoo_core moomoo_trade)
target_include_directories(strategy_cpp PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${SDK_INCLUDE}"
  "${SDK_SAMPLE}"
)
set_target_properties(strategy_cpp PROPERTIES
  BUILD_RPATH "${MOOMOO_SDK_RPATH}"
  INSTALL_RPATH "${MOOMOO_SDK_RPATH}"
  LINK_FLAGS "-no-pie"
)

add_executable(task_worker_cpp task_worker_main.cpp)
target_link_libraries(task_worker_cpp PRIVATE moomoo_core)
set_target_properties(task_worker_cpp PROPERTIES LINK_FLAGS "-no-pie")

add_executable(trade_executor_cpp trade_executor_main.cpp)
target_link_libraries(trade_executor_cpp PRIVATE moomoo_core)
set_target_properties(trade_executor_cpp PROPERTIES LINK_FLAGS "-no-pie")
