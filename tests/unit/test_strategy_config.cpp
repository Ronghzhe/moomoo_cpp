#include "config/strategy_loader.hpp"
#include "config/strategy_types.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string ReadFile(const std::string &path) {
  std::ifstream ifs(path.c_str());
  std::ostringstream oss;
  oss << ifs.rdbuf();
  return oss.str();
}

std::string FixturePath(const std::string &name) {
  return std::string(MOOMOO_FIXTURES_DIR) + "/" + name;
}

void ExpectSummaryMatchesGolden(const std::string &fixture_name,
                                const std::string &golden_name) {
  const std::string fixture = FixturePath(fixture_name);
  const std::string golden_path = FixturePath(std::string("golden/") + golden_name);
  const mmtrade::config::StrategyConfig cfg = mmtrade::config::LoadConfig(fixture);
  const nlohmann::json got = nlohmann::json::parse(mmtrade::config::ConfigToSummaryJson(cfg));
  const nlohmann::json expect = nlohmann::json::parse(ReadFile(golden_path));
  EXPECT_EQ(got, expect) << "fixture=" << fixture_name;
}

}  // namespace

// L0-01 / L0-02 from CPP_MIGRATION_TEST_PLAN.md
TEST(StrategyConfig, LoadsUsFixtureMatchingPythonGolden) {
  ExpectSummaryMatchesGolden("strategy_config_us_fixture.json",
                             "strategy_config_us_fixture.golden.json");
}

TEST(StrategyConfig, LoadsHkFixtureMatchingPythonGolden) {
  ExpectSummaryMatchesGolden("strategy_config_hk_fixture.json",
                             "strategy_config_hk_fixture.golden.json");
}

TEST(StrategyConfig, RejectsInvalidMode) {
  const std::string bad = FixturePath("_tmp_bad_mode.json");
  {
    std::ofstream ofs(bad.c_str());
    ofs << R"({
  "version": 1,
  "client_name": "x",
  "strategy_name": "t",
  "mode": "invalid",
  "opend": {"host":"127.0.0.1","port":11121},
  "account": {"trd_market":"US"},
  "symbols": [{"code":"US.AAPL","entry_side":"BUY","entry_qty":1,"status":0}]
})";
  }
  EXPECT_THROW(mmtrade::config::LoadConfig(bad), std::runtime_error);
}

TEST(StrategyConfig, ParseConditionTypedFields) {
  const mmtrade::config::StrategyConfig cfg = mmtrade::config::LoadConfig(
      FixturePath("strategy_config_hk_fixture.json"));
  ASSERT_FALSE(cfg.symbols.empty());
  const mmtrade::config::SymbolConfig &sym = cfg.symbols[0];
  ASSERT_FALSE(sym.exit_strategies.empty());

  bool found_staged = false;
  for (size_t i = 0; i < sym.exit_strategies.size(); ++i) {
    const mmtrade::config::ExitStrategyRule &rule = sym.exit_strategies[i];
    for (size_t j = 0; j < rule.conditions.size(); ++j) {
      const mmtrade::config::Condition &c = rule.conditions[j];
      if (c.kind == mmtrade::config::ConditionKind::kStagedTakeProfit) {
        found_staged = true;
        EXPECT_FALSE(c.staged_tp.targets.empty());
        EXPECT_EQ(c.staged_tp.targets.size(), c.staged_tp.fractions.size());
        EXPECT_DOUBLE_EQ(c.staged_tp.targets[0], 0.01);
      }
      if (c.kind == mmtrade::config::ConditionKind::kPnl) {
        EXPECT_FALSE(c.pnl.op.empty());
      }
    }
  }
  EXPECT_TRUE(found_staged);
}

TEST(StrategyConfig, NormalizeOrderTypeAliases) {
  const mmtrade::config::StrategyConfig cfg = mmtrade::config::LoadConfig(
      FixturePath("strategy_config_hk_fixture.json"));
  ASSERT_FALSE(cfg.symbols.empty());
  EXPECT_EQ(cfg.symbols[0].order_type, "SIM_MARKET");
}
