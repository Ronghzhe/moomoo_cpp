#pragma once

#include "config/strategy_types.hpp"
#include "log/strategy_message_format.hpp"
#include "log/strategy_store.hpp"

#include <string>

namespace mmtrade {
namespace log {

void LogTrdContext(StrategyStore *store, const config::StrategyConfig &cfg,
                   const std::string &trd_env);

void LogMonitorStartupBlock(StrategyStore *store, const config::StrategyConfig &cfg,
                            const config::SymbolConfig &sym, const std::string &trd_env);

void LogStrategyReady(StrategyStore *store, const std::string &symbol, bool telegram_enabled,
                      bool startup_notify_once);

void LogLotSize(StrategyStore *store, const std::string &code, int lot_size);

void LogReadMarket(StrategyStore *store, const std::string &code, double bid, double ask);

void LogEntryJudge(StrategyStore *store, const std::string &code, const std::string &rule_id,
                   int cond_idx, const std::string &type_name, const std::string &reason, bool ok,
                   double plan_qty, double est_notional);

void LogStartTrade(StrategyStore *store, const std::string &code, const std::string &hit_label);

void LogExitJudge(StrategyStore *store, const std::string &code, const std::string &rule_id,
                  int cond_idx, const std::string &type_name, const std::string &reason, bool ok,
                  double plan_qty, double est_notional);

void LogStartExit(StrategyStore *store, const std::string &code, const std::string &hit_label);

void LogOrderSizing(StrategyStore *store, const config::SymbolConfig &sym, double ref_px,
                    double qty, int lot_size, const std::string &note);

void LogOrderRequest(StrategyStore *store, const config::SymbolConfig &sym, const std::string &side,
                     double qty, double submit_px, double ref_px, const std::string &trd_env,
                     const std::string &remark);

void LogPlaceOrderResult(StrategyStore *store, bool ok, const std::string &order_id,
                         const std::string &status, const std::string &error);

void LogAwaitFill(StrategyStore *store, const std::string &order_id, bool is_entry);

void LogOrderPoll(StrategyStore *store, const std::string &code, const std::string &order_id,
                  const std::string &status, double dealt, double target, bool is_exit,
                  const std::string &outcome);

void LogEntryFillBlock(StrategyStore *store, const config::StrategyConfig &cfg,
                       const config::SymbolConfig &sym, double fill_qty, double fill_px,
                       double exp_px, const std::string &order_id, const std::string &trd_env,
                       const std::string &hit_rule_id);

void LogExitFillBlock(StrategyStore *store, const config::StrategyConfig &cfg,
                      const config::SymbolConfig &sym, double fill_qty, double fill_px,
                      double exp_px, const std::string &order_id, const std::string &trd_env,
                      const std::string &exit_rule_id);

}  // namespace log
}  // namespace mmtrade
