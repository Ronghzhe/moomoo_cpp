include(FetchContent)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(l0_tests
  unit/test_book_mailbox.cpp
  unit/test_order_parse.cpp
  unit/test_hk_tick.cpp
  unit/test_order_push_cache.cpp
  unit/test_strategy_config.cpp
  unit/test_rate_limiter.cpp
  unit/test_gateway_components.cpp
  unit/test_order_fail_retry.cpp
  unit/test_order_session_policy.cpp
  unit/test_pipeline_latency.cpp
  unit/test_kline_parse.cpp
  unit/test_account_config.cpp
  unit/test_broker.cpp
  unit/test_condition_eval.cpp
  unit/test_strategy_judge.cpp
  unit/test_config_path_resolver.cpp
  unit/test_strategy_loop.cpp
  unit/test_strategy_store.cpp
  unit/test_p0_parity.cpp
  unit/test_entry_net_planner.cpp
  unit/test_net_split_rollback.cpp
  unit/test_net_split_phase_a2.cpp
  unit/test_net_split_leg_wait.cpp
  unit/test_net_split_broker.cpp
  unit/test_task_db.cpp
  unit/test_task_repository_integration.cpp
  unit/test_task_mapper.cpp
  unit/test_tg_secrets_loader.cpp
  unit/test_telegram_client.cpp
  unit/test_config_persist.cpp
  unit/test_strategy_message_format.cpp
  unit/test_l2_golden_replay.cpp
  ${CMAKE_SOURCE_DIR}/src/strategy/live_eval_inject_stub.cpp
)

target_compile_definitions(l0_tests PRIVATE
  MOOMOO_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
  MOOMOO_REPLAY_DIR="${CMAKE_SOURCE_DIR}/tests/replay"
  MOOMOO_GOLDEN_L2_DIR="${CMAKE_SOURCE_DIR}/tests/golden/l2"
)

target_link_libraries(l0_tests PRIVATE moomoo_core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(l0_tests)
