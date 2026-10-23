#pragma once

#include "config/strategy_types.hpp"
#include "log/strategy_store.hpp"
#include "opend/quote_snapshot.hpp"
#include "strategy/book_guard.hpp"
#include "strategy/net_split_leg_wait.hpp"
#include "strategy/symbol_runtime.hpp"

#include <cstdint>
#include <string>

namespace mmtrade {
namespace strategy {

enum class PendingKind {
  kNone,
  kEntry,
  kExit,
};

struct PendingOrderState {
  PendingKind kind;
  std::uint64_t order_id;
  std::string order_id_ex;
  double target_qty;
  double placed_mono;
  double last_poll_mono;
  double submit_price;
  double ref_px;
  std::string exit_rule_id;
  std::string side;
  std::string order_type_used;
  int replace_count;
  int retry_count;
  bool book_guard_emergency;
  bool book_guard_emergency_limit;
  int hk_short_place_attempt;
  double hk_short_band_lo;
  double hk_short_band_hi;
  double hk_short_band_ref_ask;

  PendingOrderState()
      : kind(PendingKind::kNone),
        order_id(0),
        target_qty(0.0),
        placed_mono(0.0),
        last_poll_mono(0.0),
        submit_price(0.0),
        ref_px(0.0),
        replace_count(0),
        retry_count(0),
        book_guard_emergency(false),
        book_guard_emergency_limit(false),
        hk_short_place_attempt(0),
        hk_short_band_lo(0.0),
        hk_short_band_hi(0.0),
        hk_short_band_ref_ask(0.0) {}
};

struct SymbolLoopState {
  config::SymbolConfig sym;
  SymbolEvalRuntime eval_rt;
  BookGuardRuntime book_guard;
  bool entry_done;
  bool exit_done;
  bool entry_await_fill;
  bool exit_await_fill;
  int lot_size;
  double place_cooldown_until_mono;
  PendingOrderState pending;

  bool symbol_tradable;
  std::string symbol_tradable_reason;
  double last_symbol_tradability_poll_ts;
  double last_symbol_tradability_log_ts;

  int hk_short_place_attempt;
  double hk_short_band_ref_ask;
  double hk_short_band_lo;
  double hk_short_band_hi;
  double hk_short_reprice_pause_until;
  double last_hk_short_band_skip_log_ts;

  bool entry_blocked;
  std::string trading_fatal_code;
  std::string trading_fatal_detail;
  std::string last_entry_hit_rule_id;

  int entry_retry_failed_count;
  int exit_retry_failed_count;
  bool entry_force_market_next;
  bool exit_force_market_next;
  std::string last_market_phase;

  NetSplitRetryState net_split_retry;

  struct LegLatencyOnce {
    bool place_start;
    bool push_first;
    bool poll_first;
    bool push_submitting;
    bool push_filled;
    bool fill_source;

    LegLatencyOnce()
        : place_start(false),
          push_first(false),
          poll_first(false),
          push_submitting(false),
          push_filled(false),
          fill_source(false) {}
  };

  LegLatencyOnce entry_latency;
  LegLatencyOnce exit_latency;

  SymbolLoopState()
      : entry_done(false),
        exit_done(false),
        entry_await_fill(false),
        exit_await_fill(false),
        lot_size(1),
        place_cooldown_until_mono(0.0),
        symbol_tradable(true),
        last_symbol_tradability_poll_ts(0.0),
        last_symbol_tradability_log_ts(0.0),
        hk_short_place_attempt(0),
        hk_short_band_ref_ask(0.0),
        hk_short_band_lo(0.0),
        hk_short_band_hi(0.0),
        hk_short_reprice_pause_until(0.0),
        last_hk_short_band_skip_log_ts(0.0),
        entry_blocked(false),
        entry_retry_failed_count(0),
        exit_retry_failed_count(0),
        entry_force_market_next(false),
        exit_force_market_next(false),
        last_market_phase() {}
};

bool IsAwaitingFill(const SymbolLoopState &st);

int StatusAfterEntryFill(double opened_qty, double entry_qty);

bool ExitCycleComplete(const config::SymbolConfig &sym);

bool SymbolManagedDone(const SymbolLoopState &st);

bool RefreshSymbolTradability(SymbolLoopState *st, const opend::QuoteSnapshot &snap,
                              double now_ts, log::StrategyStore *store);

}  // namespace strategy
}  // namespace mmtrade
