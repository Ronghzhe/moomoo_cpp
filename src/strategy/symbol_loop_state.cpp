#include "strategy/symbol_loop_state.hpp"

#include "strategy/symbol_tradability.hpp"

#include <iostream>

namespace mmtrade {
namespace strategy {

bool RefreshSymbolTradability(SymbolLoopState *st, const opend::QuoteSnapshot &snap,
                              double now_ts, log::StrategyStore *store) {
  if (st == NULL) {
    return true;
  }
  const double poll_sec = SymbolTradabilityPollSec();
  if (now_ts - st->last_symbol_tradability_poll_ts < poll_sec) {
    return st->symbol_tradable;
  }
  st->last_symbol_tradability_poll_ts = now_ts;
  const bool prev = st->symbol_tradable;
  const std::string prev_reason = st->symbol_tradable_reason;
  if (!snap.has_tradability) {
    return st->symbol_tradable;
  }
  const TradabilityAssessResult assess =
      AssessSymbolTradability(snap.sec_status, snap.suspended, snap.last_price);
  st->symbol_tradable = assess.tradable;
  st->symbol_tradable_reason = assess.reason;
  if (!prev && assess.tradable && store != NULL) {
    store->LogEvent("INFO", "symbol_tradability_recovered",
                    "code=" + st->sym.code + " prev_reason=" + prev_reason);
  }
  if (!assess.tradable && now_ts - st->last_symbol_tradability_log_ts >= 60.0) {
    st->last_symbol_tradability_log_ts = now_ts;
    if (store != NULL) {
      store->LogEvent("WARN", "symbol_not_tradable", st->sym.code + " " + assess.reason);
    }
    std::cerr << "symbol_tradability skip code=" << st->sym.code << " reason=" << assess.reason
              << "\n";
  }
  return st->symbol_tradable;
}

bool IsAwaitingFill(const SymbolLoopState &st) {
  return st.pending.kind != PendingKind::kNone && st.pending.order_id != 0;
}

int StatusAfterEntryFill(double opened_qty, double entry_qty) {
  if (entry_qty > 0.0 && opened_qty + 1e-9 < entry_qty) {
    return 1;
  }
  return 2;
}

bool ExitCycleComplete(const config::SymbolConfig &sym) {
  const double open_left = sym.opened_qty - sym.closed_qty;
  if (sym.exit_qty > 0.0 && sym.closed_qty + 1e-9 >= sym.exit_qty) {
    return true;
  }
  return open_left <= 1e-9 && sym.closed_qty > 0.0;
}

bool SymbolManagedDone(const SymbolLoopState &st) {
  if (st.exit_done) {
    return true;
  }
  return !st.sym.exit.enabled && st.entry_done;
}

}  // namespace strategy
}  // namespace mmtrade
