#include "config/strategy_loader.hpp"
#include "log/strategy_message_format.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

std::string FixturePath(const std::string &name) {
  return std::string(MOOMOO_FIXTURES_DIR) + "/" + name;
}

}  // namespace

TEST(StrategyMessageFormat, EntryFillIncludesTriggerConditions) {
  const mmtrade::config::StrategyConfig cfg =
      mmtrade::config::LoadConfig(FixturePath("strategy_config_hk_fixture.json"));
  ASSERT_FALSE(cfg.symbols.empty());
  const mmtrade::config::SymbolConfig sym = cfg.symbols[0];
  const std::string msg = mmtrade::log::FormatEntryFillMessage(
      cfg, sym, 200.0, 10.83, 10.94, "oid123", "paper", "S1");
  EXPECT_NE(msg.find("触发开仓条件"), std::string::npos);
  EXPECT_NE(msg.find("预设平仓条件"), std::string::npos);
  EXPECT_NE(msg.find("立即开仓"), std::string::npos);
}

TEST(StrategyMessageFormat, ExitFillIncludesTriggerConditions) {
  const mmtrade::config::StrategyConfig cfg =
      mmtrade::config::LoadConfig(FixturePath("strategy_config_hk_fixture.json"));
  ASSERT_FALSE(cfg.symbols.empty());
  const mmtrade::config::SymbolConfig sym = cfg.symbols[0];
  const std::string msg = mmtrade::log::FormatExitFillMessage(
      cfg, sym, "SELL", 200.0, 10.81, 10.70, 10.83, "oid456", "普通orderbook平仓", "paper");
  EXPECT_NE(msg.find("触发平仓条件"), std::string::npos);
  EXPECT_NE(msg.find("立即平仓"), std::string::npos);
}

TEST(StrategyMessageFormat, MonitorStartupUsesLongBuyLongSell) {
  mmtrade::config::StrategyConfig cfg;
  cfg.strategy_name = "short_squeeze";
  mmtrade::config::SymbolConfig sym;
  sym.code = "HK.02208";
  sym.entry_side = "BUY";
  sym.entry_qty = 200;
  sym.task_id = "bench_test";
  sym.status = 0;
  sym.exit.enabled = false;
  const std::string open_msg = mmtrade::log::FormatMonitorStartupMessage(cfg, sym, "paper");
  EXPECT_NE(open_msg.find("HK.02208-long-buy"), std::string::npos);
  EXPECT_EQ(open_msg.find("buy-buy"), std::string::npos);

  sym.status = 2;
  sym.opened_qty = 200;
  sym.opened_price = 8.15;
  sym.exit.enabled = true;
  mmtrade::config::ExitStrategyRule xr;
  xr.rule_id = "X1";
  xr.enabled = true;
  mmtrade::config::Condition c;
  c.kind = mmtrade::config::ConditionKind::kPrice;
  c.price.op = "";
  xr.conditions.push_back(c);
  sym.exit_strategies.push_back(xr);
  const std::string close_msg = mmtrade::log::FormatMonitorStartupMessage(cfg, sym, "paper");
  EXPECT_NE(close_msg.find("HK.02208-long-sell"), std::string::npos);
  EXPECT_EQ(close_msg.find("buy-sell"), std::string::npos);
}

TEST(StrategyMessageFormat, SimMarketExecCnTrimsTrailingZeros) {
  mmtrade::config::SymbolConfig sym;
  sym.code = "HK.02208";
  sym.order_type = "sim_market";
  sym.sim_market_pct = 0.01;
  EXPECT_EQ(mmtrade::log::FormatOrderExecCn(sym), "1%限仿市");
  sym.code = "US.AAPL";
  sym.sim_market_pct = 0.02;
  EXPECT_EQ(mmtrade::log::FormatOrderExecCn(sym), "2%限仿市");
  EXPECT_EQ(mmtrade::log::FormatOrderExecCn(sym, 0.015), "1.5%限仿市");
}
