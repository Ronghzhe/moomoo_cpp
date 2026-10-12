#include "config/strategy_loader.hpp"
#include "strategy/rule_logic.hpp"
#include "strategy/exchange_clock.hpp"
#include "strategy/tick_builder.hpp"
#include "strategy/strategy_runner.hpp"
#include "strategy/symbol_runtime.hpp"
#include "strategy/tick_context.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

std::string FixturePath(const std::string &name) {
  return std::string(MOOMOO_FIXTURES_DIR) + "/" + name;
}

mmtrade::config::SymbolConfig LoadUsSymbol() {
  const mmtrade::config::StrategyConfig cfg =
      mmtrade::config::LoadConfig(FixturePath("strategy_config_us_fixture.json"));
  EXPECT_FALSE(cfg.symbols.empty());
  return cfg.symbols[0];
}

}  // namespace

TEST(RuleLogic, AllAndAny) {
  const std::vector<bool> all_true(2, true);
  const std::vector<bool> mixed = {true, false};
  EXPECT_TRUE(mmtrade::strategy::BoolMerge(all_true, "all"));
  EXPECT_FALSE(mmtrade::strategy::BoolMerge(mixed, "all"));
  EXPECT_TRUE(mmtrade::strategy::BoolMerge(mixed, "any"));
  EXPECT_FALSE(mmtrade::strategy::BoolMerge(std::vector<bool>(), "any"));
}

TEST(TickBuilder, SetsMidAndGainFractionForLong) {
  const mmtrade::config::SymbolConfig sym = LoadUsSymbol();
  mmtrade::strategy::BookTick book;
  book.valid = true;
  book.bid_1 = 110.0;
  book.ask_1 = 112.0;
  mmtrade::config::SymbolConfig sym_copy = sym;
  sym_copy.opened_price = 100.0;
  const mmtrade::strategy::StrategyTickContext tick =
      mmtrade::strategy::BuildTickContext(sym_copy, book, 1000.0, sym_copy.code);
  EXPECT_TRUE(tick.has_bid_ask);
  mmtrade::engine::FieldMap::const_iterator mid_it = tick.fields.find("mid");
  ASSERT_NE(mid_it, tick.fields.end());
  EXPECT_DOUBLE_EQ(mid_it->second, 111.0);
  EXPECT_TRUE(tick.has_gain_fraction);
  EXPECT_NEAR(tick.gain_fraction, 0.10, 1e-9);
  EXPECT_TRUE(tick.has_exchange_local_now);
}

TEST(ExchangeClock, UsCodeUsesUtcOffset) {
  const mmtrade::engine::LocalDateTime local =
      mmtrade::strategy::ExchangeLocalFromUnix("US.AAPL", 0.0);
  EXPECT_EQ(local.year, 1969);
  EXPECT_EQ(local.month, 12);
  EXPECT_EQ(local.day, 31);
}

TEST(StrategyRunner, EntryAndExitViaRunSymbolJudge) {
  const mmtrade::config::SymbolConfig sym = LoadUsSymbol();
  mmtrade::strategy::SymbolEvalRuntime rt;
  mmtrade::strategy::BookTick book;
  book.valid = true;
  book.bid_1 = 24.0;
  book.ask_1 = 14.0;
  const mmtrade::strategy::StrategyTickContext tick =
      mmtrade::strategy::BuildTickContext(sym, book, 1.0, sym.code);
  const mmtrade::strategy::SymbolJudgeOutcome out =
      mmtrade::strategy::RunSymbolJudge(sym, tick, &rt, NULL);
  EXPECT_TRUE(out.entry_hit);
  EXPECT_TRUE(out.exit_hit);
}

TEST(StrategyJudge, EntryOrderbookRuleHits) {
  const mmtrade::config::SymbolConfig sym = LoadUsSymbol();
  mmtrade::strategy::SymbolEvalRuntime rt;
  mmtrade::strategy::StrategyTickContext tick;
  tick.now = 1.0;
  tick.has_bid_ask = true;
  tick.ask_1 = 14.0;
  tick.bid_1 = 13.9;

  const mmtrade::strategy::StrategyJudgeResult out =
      mmtrade::strategy::JudgeEntryStrategies(sym, tick, &rt, NULL);
  EXPECT_TRUE(out.hit);
  EXPECT_EQ(out.hit_rule_id, "S1_OPEN");
}

TEST(StrategyJudge, EntryOrderbookRuleMisses) {
  const mmtrade::config::SymbolConfig sym = LoadUsSymbol();
  mmtrade::strategy::SymbolEvalRuntime rt;
  mmtrade::strategy::StrategyTickContext tick;
  tick.now = 1.0;
  tick.has_bid_ask = true;
  tick.ask_1 = 16.0;

  const mmtrade::strategy::StrategyJudgeResult out =
      mmtrade::strategy::JudgeEntryStrategies(sym, tick, &rt, NULL);
  EXPECT_FALSE(out.hit);
}

TEST(StrategyJudge, ExitOrderbookRuleHits) {
  const mmtrade::config::SymbolConfig sym = LoadUsSymbol();
  mmtrade::strategy::SymbolEvalRuntime rt;
  mmtrade::strategy::StrategyTickContext tick;
  tick.now = 1.0;
  tick.has_bid_ask = true;
  tick.bid_1 = 24.0;
  tick.ask_1 = 24.1;

  const mmtrade::strategy::StrategyJudgeResult out =
      mmtrade::strategy::JudgeExitStrategies(sym, tick, &rt, NULL);
  EXPECT_TRUE(out.hit);
  EXPECT_EQ(out.hit_rule_id, "E1_CLOSE");
}

TEST(StrategyJudge, ExitPnlRuleWithGainFraction) {
  const mmtrade::config::SymbolConfig sym = LoadUsSymbol();
  mmtrade::strategy::SymbolEvalRuntime rt;
  mmtrade::strategy::StrategyTickContext tick;
  tick.now = 1.0;
  tick.has_gain_fraction = true;
  tick.gain_fraction = 0.12;

  mmtrade::config::SymbolConfig sym_copy = sym;
  for (size_t i = 0; i < sym_copy.exit_strategies.size(); ++i) {
    if (sym_copy.exit_strategies[i].rule_id == "E3_CLOSE") {
      sym_copy.exit_strategies[i].enabled = true;
    }
    if (sym_copy.exit_strategies[i].rule_id == "E1_CLOSE") {
      sym_copy.exit_strategies[i].enabled = false;
    }
  }

  const mmtrade::strategy::StrategyJudgeResult out =
      mmtrade::strategy::JudgeExitStrategies(sym_copy, tick, &rt, NULL);
  EXPECT_TRUE(out.hit);
  EXPECT_EQ(out.hit_rule_id, "E3_CLOSE");
}
