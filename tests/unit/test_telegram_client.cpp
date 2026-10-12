#include "config/strategy_types.hpp"
#include "log/strategy_message_format.hpp"
#include "notify/telegram_client.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <string>

TEST(TelegramClient, PayloadPreservesChineseUtf8) {
  const std::string text =
      "【开仓】====2026-08-18 11:11:51\n"
      "触发开仓条件------------------------\n"
      "·立即开仓 (1%限仿市)\n";
  const std::string payload =
      mmtrade::notify::BuildTelegramSendMessagePayload("-5560998729", text);
  const nlohmann::json doc = nlohmann::json::parse(payload);
  EXPECT_EQ(doc.at("chat_id").get<std::string>(), "-5560998729");
  EXPECT_EQ(doc.at("text").get<std::string>(), text);
  EXPECT_NE(payload.find("【开仓】"), std::string::npos);
  EXPECT_EQ(payload.find("\\u"), std::string::npos);
}

TEST(TelegramClient, EntryFillMessageIsValidUtf8JsonPayload) {
  mmtrade::config::SymbolConfig sym;
  sym.code = "HK.02208";
  sym.entry_side = "BUY";
  sym.entry_qty = 200;
  sym.order_type = "sim_market";
  sym.sim_market_pct = 0.01;
  sym.task_id = "TradeTest_HK_02208_short_squeeze_20260818";
  sym.exit.enabled = true;
  mmtrade::config::EntryStrategyRule entry_rule;
  entry_rule.rule_id = "S1";
  entry_rule.enabled = true;
  mmtrade::config::Condition cond;
  cond.kind = mmtrade::config::ConditionKind::kPrice;
  cond.price.field = "ask_1";
  cond.price.op = "";
  entry_rule.conditions.push_back(cond);
  sym.entry_strategies.push_back(entry_rule);
  mmtrade::config::ExitStrategyRule exit_rule;
  exit_rule.rule_id = "E1_CLOSE";
  exit_rule.enabled = true;
  exit_rule.side = "SELL";
  mmtrade::config::Condition xcond;
  xcond.kind = mmtrade::config::ConditionKind::kPrice;
  xcond.price.field = "bid_1";
  xcond.price.op = "";
  exit_rule.conditions.push_back(xcond);
  sym.exit_strategies.push_back(exit_rule);

  mmtrade::config::StrategyConfig cfg;
  cfg.mode = "paper";
  const std::string msg = mmtrade::log::FormatEntryFillMessage(cfg, sym, 200.0, 10.83, 10.94,
                                                             "3240264", "paper", "S1");
  const std::string payload = mmtrade::notify::BuildTelegramSendMessagePayload("-5560998729", msg);
  const nlohmann::json doc = nlohmann::json::parse(payload);
  EXPECT_EQ(doc.at("text").get<std::string>(), msg);
  EXPECT_NE(msg.find("触发开仓条件"), std::string::npos);
}
