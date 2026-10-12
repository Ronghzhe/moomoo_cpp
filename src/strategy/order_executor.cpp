#include "strategy/order_executor.hpp"

#include "market/hk_tick.hpp"
#include "strategy/book_guard.hpp"
#include "strategy/hk_short_params.hpp"
#include "strategy/trade_side_util.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace mmtrade {
namespace strategy {

namespace {

std::string Upper(const std::string &s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return out;
}

bool IsMarketLike(const std::string &order_type) {
  const std::string ot = Upper(order_type);
  return ot == "MARKET" || ot == "SIM_MARKET";
}

bool IsHkCode(const std::string &code) {
  return code.size() >= 3 && code[0] == 'H' && code[1] == 'K' && code[2] == '.';
}

double SimMarketPctForAttempt(const std::string &code, int attempt_index) {
  static const double kUs[] = {0.02, 0.015, 0.01};
  static const double kHk[] = {0.01, 0.005, 0.0025};
  const double *seq = IsHkCode(code) ? kHk : kUs;
  const size_t n = IsHkCode(code) ? 3 : 3;
  const int idx = attempt_index < 0 ? 0 : attempt_index;
  if (static_cast<size_t>(idx) < n) {
    return seq[static_cast<size_t>(idx)];
  }
  return seq[n - 1];
}

bool IsUsCode(const std::string &code) {
  return code.size() >= 3 && code[0] == 'U' && code[1] == 'S' && code[2] == '.';
}

double AlignUsPriceToTick(double raw_px, const std::string &side) {
  if (raw_px <= 0.0) {
    return raw_px;
  }
  // Mirrors Python strategy_main._submit_price_for_execution US branch ($0.01 tick).
  const double step = 0.01;
  const double n = raw_px / step;
  const std::string s = Upper(NormalizeTradeSide(side));
  const double q = (s == "BUY") ? std::ceil(n - 1e-12) : std::floor(n + 1e-12);
  const double aligned = q * step;
  return aligned > 0.0 ? aligned : raw_px;
}

double AlignPriceIfNeeded(const std::string &code, const std::string &side, double px,
                          double ref_px) {
  if (px <= 0.0) {
    return px;
  }
  if (IsHkCode(code)) {
    return mmtrade::market::AlignPriceToHkTick(px, side.c_str(), ref_px, NULL);
  }
  if (IsUsCode(code)) {
    return AlignUsPriceToTick(px, side);
  }
  return px;
}

}  // namespace

double ComputeSubmitPriceForAttempt(const config::SymbolConfig &sym, const BookTick &book,
                                    const std::string &side, int attempt_index,
                                    bool market_fallback) {
  const std::string s = Upper(NormalizeTradeSide(side));
  if (!book.valid || BookGuardShouldSkip(book.bid_1, book.ask_1)) {
    return 0.01;
  }
  const std::string ot = market_fallback ? "MARKET" : Upper(sym.order_type);
  const double pct =
      market_fallback ? 0.0 : SimMarketPctForAttempt(sym.code, attempt_index);
  double px = 0.01;
  if (IsMarketLike(ot)) {
    if (s == "BUY") {
      px = book.ask_1 > 0.0 ? book.ask_1 * (1.0 + pct) : 0.01;
    } else if (IsHkShortOpenEntry(sym.code, s, sym.entry_side)) {
      px = book.ask_1 > 0.0 ? book.ask_1 : 0.01;
    } else {
      px = book.bid_1 > 0.0 ? book.bid_1 * (1.0 - pct) : 0.01;
    }
  } else if (s == "BUY") {
    px = book.ask_1 > 0.0 ? book.ask_1 : 0.01;
  } else if (IsHkShortOpenEntry(sym.code, s, sym.entry_side)) {
    px = book.ask_1 > 0.0 ? book.ask_1 : 0.01;
  } else {
    px = book.bid_1 > 0.0 ? book.bid_1 : 0.01;
  }
  const double ref = s == "BUY" ? book.ask_1 : book.bid_1;
  return AlignPriceIfNeeded(sym.code, s, px, ref);
}

double ComputeSubmitPrice(const config::SymbolConfig &sym, const BookTick &book,
                          const std::string &side) {
  return ComputeSubmitPriceForAttempt(sym, book, side, 0, false);
}

double ResolveEntryQty(const config::SymbolConfig &sym) {
  if (sym.entry_qty > 0.0) {
    return sym.entry_qty;
  }
  if (sym.initial_qty > 0.0) {
    return sym.initial_qty;
  }
  return 1.0;
}

double ResolveExitQty(const config::SymbolConfig &sym) {
  const double open_left = sym.opened_qty > sym.closed_qty ? (sym.opened_qty - sym.closed_qty) : 0.0;
  if (sym.exit_qty > 0.0) {
    return std::min(open_left > 0.0 ? open_left : sym.exit_qty, sym.exit_qty);
  }
  return open_left > 0.0 ? open_left : sym.opened_qty;
}

}  // namespace strategy
}  // namespace mmtrade
