#include "strategy/tick_builder.hpp"

#include "strategy/exchange_clock.hpp"
#include "strategy/trade_side_util.hpp"

namespace mmtrade {
namespace strategy {

StrategyTickContext BuildTickContext(const config::SymbolConfig &sym, const BookTick &book,
                                     double now, const std::string &code_full) {
  StrategyTickContext tick;
  tick.now = now;
  tick.has_bid_ask = book.valid;
  if (book.valid) {
    tick.bid_1 = book.bid_1;
    tick.ask_1 = book.ask_1;
    tick.fields["bid_1"] = book.bid_1;
    tick.fields["ask_1"] = book.ask_1;
    const double mid = (book.bid_1 + book.ask_1) / 2.0;
    tick.fields["mid"] = mid;
  }
  tick.has_exchange_local_now = true;
  tick.exchange_local_now = ExchangeLocalFromUnix(code_full, now);

  if (book.valid && sym.opened_price > 0.0) {
    const double px =
        (NormalizeTradeSide(sym.entry_side) == "BUY") ? book.bid_1 : book.ask_1;
    if (px > 0.0) {
      if (NormalizeTradeSide(sym.entry_side) == "BUY") {
        tick.gain_fraction = (px - sym.opened_price) / sym.opened_price;
      } else {
        tick.gain_fraction = (sym.opened_price - px) / sym.opened_price;
      }
      tick.has_gain_fraction = true;
    }
  }
  return tick;
}

}  // namespace strategy
}  // namespace mmtrade
