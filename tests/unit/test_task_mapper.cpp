#include "task/task_mapper.hpp"
#include "task/task_params_spec.hpp"

#include <gtest/gtest.h>

TEST(TaskMapper, EntryNowBuildsOrderbookRule) {
  nlohmann::json payload = {
      {"task_time", 1785245729999LL},
      {"side", "long"},
      {"order_type", "market"},
      {"price", nullptr},
      {"task_type", "entry_now"},
      {"stock_type", "HK"},
      {"stock_code", "02208"},
      {"entry_conditions", nlohmann::json::array()},
      {"exit_conditions", nlohmann::json::array()},
      {"trade_qty", 200},
      {"trade_amount", nullptr},
      {"note", "跳空"},
  };
  mmtrade::task::ValidateTaskParams(payload);
  const mmtrade::task::MappedConfigResult out =
      mmtrade::task::BuildStrategyConfig("cpp_test_HK_02208", payload, true);
  EXPECT_EQ(out.config.at("mode"), "paper");
  const nlohmann::json &sym = out.config.at("symbols").at(0);
  EXPECT_EQ(sym.at("code"), "HK.02208");
  EXPECT_EQ(sym.at("entry_side"), "BUY");
  EXPECT_TRUE(sym.at("entry_strategies").is_array());
  EXPECT_EQ(sym.at("entry_strategies").size(), 1u);
  EXPECT_EQ(sym.at("exit_strategies").size(), 0u);
  EXPECT_FALSE(sym.at("exit_enabled").get<bool>());
}

TEST(TaskParamsSpec, RejectsMissingKeys) {
  nlohmann::json payload = nlohmann::json::object();
  EXPECT_THROW(mmtrade::task::ValidateTaskParams(payload), std::runtime_error);
}
