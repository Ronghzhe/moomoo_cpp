#pragma once

#include "config/strategy_types.hpp"
#include "opend/quote_snapshot.hpp"
#include "strategy/symbol_loop_state.hpp"
#include "strategy/tick_context.hpp"

#include <string>

namespace mmtrade {
namespace log {
class StrategyStore;
}
namespace opend {
class OrderClient;
}
namespace strategy {

struct FillHooks {
  const config::StrategyConfig *cfg;
  std::string config_path;
  std::string trd_env;
  log::StrategyStore *store;
};

enum class PendingAdvanceResult {
  kIdle,
  kWaiting,
  kFilled,
  kFailed,
  kReplaced,
  kFatal,
};

struct PlaceAttemptResult {
  bool ok;
  std::string error;

  PlaceAttemptResult() : ok(false) {}
};

PlaceAttemptResult PlacePendingOrder(SymbolLoopState *st, opend::OrderClient *client,
                                     PendingKind kind, const std::string &side, double qty,
                                     double price, const std::string &order_type,
                                     bool book_guard_emergency, bool book_guard_emergency_limit,
                                     double ref_px, log::StrategyStore *store,
                                     const std::string &trd_env, const std::string &exit_rule_id,
                                     const std::string &remark = "");

PendingAdvanceResult AdvancePendingOrder(SymbolLoopState *st, const config::StrategyConfig &cfg,
                                         opend::OrderClient *client, const BookTick &book,
                                         const opend::QuoteSnapshot &snap, double now_ts,
                                         double now_mono, const FillHooks *hooks);

bool TryPlaceEntry(SymbolLoopState *st, const config::StrategyConfig &cfg, opend::OrderClient *client,
                   const BookTick &book, const opend::QuoteSnapshot &snap, double now_ts,
                   double now_mono, log::StrategyStore *store, const std::string &trd_env);

bool TryPlaceExit(SymbolLoopState *st, const config::StrategyConfig &cfg, opend::OrderClient *client,
                  const BookTick &book, const opend::QuoteSnapshot &snap, double now_ts,
                  double now_mono, const std::string &exit_rule_id, log::StrategyStore *store,
                  const std::string &trd_env);

void ApplyEntryFill(SymbolLoopState *st, double fill_qty, double fill_px, int lot_size);

void ApplyExitFill(SymbolLoopState *st, double fill_qty, double fill_px);

}  // namespace strategy
}  // namespace mmtrade
