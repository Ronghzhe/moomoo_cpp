#pragma once

#include "config/strategy_types.hpp"
#include "strategy/symbol_loop_state.hpp"

#include <string>

namespace mmtrade {
namespace log {
class StrategyStore;
}
namespace strategy {

enum class OrderFailBumpResult {
  kContinue,  // scheduled wait / count bumped; keep running
  kFatal,     // trading_fatal set; stop placing
};

struct OrderTypeRetryChoice {
  std::string order_type;
  bool market_fallback;

  OrderTypeRetryChoice() : market_fallback(false) {}
  OrderTypeRetryChoice(const std::string &ot, bool mf) : order_type(ot), market_fallback(mf) {}
};

double OrderCancelRetryWaitSec(const config::StrategyConfig &cfg);
double OrderLimitPriceRetryWaitSec(const config::StrategyConfig &cfg);
double OrderTooFastRetryWaitSec(const config::StrategyConfig &cfg);
double OrderRateLimitRetryWaitSec(const config::StrategyConfig &cfg);

// Wait seconds for a classified retriable failure (mirrors Python branch waits).
double OrderFailRetryWaitSec(const config::StrategyConfig &cfg, const std::string &status,
                             const std::string &fail_detail);

OrderTypeRetryChoice ResolveEntryOrderTypeForRetry(SymbolLoopState *st,
                                                   const config::StrategyConfig &cfg);
OrderTypeRetryChoice ResolveExitOrderTypeForRetry(SymbolLoopState *st,
                                                  const config::StrategyConfig &cfg);

void SetTradingFatal(SymbolLoopState *st, const std::string &fatal_code,
                     const std::string &detail, log::StrategyStore *store,
                     const std::string &log_key, const std::string &log_line);

// Mirrors strategy_main._entry_bump_fail_in_rth / _exit_bump_fail_in_rth (in_rth=true).
// Caller should clear pending/await flags before or after; this sets cooldown + fatal/force_market.
OrderFailBumpResult BumpEntryOrderFail(SymbolLoopState *st, const config::StrategyConfig &cfg,
                                       double now_mono, const std::string &fail_detail,
                                       const std::string &oid, const std::string &status,
                                       log::StrategyStore *store,
                                       const std::string &immediate_fatal_code = "ENTRY_ORDER_FAILED");

OrderFailBumpResult BumpExitOrderFail(SymbolLoopState *st, const config::StrategyConfig &cfg,
                                      double now_mono, const std::string &fail_detail,
                                      const std::string &oid, const std::string &status,
                                      log::StrategyStore *store,
                                      const std::string &immediate_fatal_code = "EXIT_ORDER_FAILED");

}  // namespace strategy
}  // namespace mmtrade
