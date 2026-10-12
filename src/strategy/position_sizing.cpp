#include "strategy/position_sizing.hpp"

#include <cmath>
#include <sstream>

namespace mmtrade {
namespace strategy {

EntrySizingResult SizeQtyFromBudget(double budget, double price, int lot_size) {
  EntrySizingResult out;
  const int L = std::max(1, lot_size);
  if (price <= 0.0) {
    out.note = "price<=0";
    return out;
  }
  const double raw = budget / price;
  const int n_lots = static_cast<int>(raw / static_cast<double>(L));
  out.qty = static_cast<double>(n_lots * L);
  std::ostringstream oss;
  oss << "lot_size=" << L << " budget/price~" << raw << " n_lots=" << n_lots << " qty=" << out.qty;
  out.note = oss.str();
  out.ok = n_lots >= 1;
  return out;
}

double EntryQtyRemain(const config::SymbolConfig &sym) {
  if (sym.entry_qty <= 0.0) {
    return 0.0;
  }
  return std::max(0.0, sym.entry_qty - sym.opened_qty);
}

bool EntryResidualCannotPlaceWholeLot(double remain, int lot_size) {
  const int L = std::max(1, lot_size);
  if (remain <= 1e-9) {
    return true;
  }
  return remain + 1e-9 < static_cast<double>(L);
}

double ResolveEntryQtyForPlace(const config::SymbolConfig &sym, double price, int lot_size,
                               std::string *note) {
  if (sym.budget > 0.0 && sym.entry_qty <= 0.0) {
    const EntrySizingResult sized = SizeQtyFromBudget(sym.budget, price, lot_size);
    if (note != NULL) {
      *note = sized.note;
    }
    return sized.ok ? sized.qty : 0.0;
  }
  if (sym.entry_qty > 0.0) {
    const double remain = EntryQtyRemain(sym);
    if (remain > 0.0) {
      return remain;
    }
    return sym.entry_qty;
  }
  if (sym.initial_qty > 0.0) {
    return sym.initial_qty;
  }
  return static_cast<double>(std::max(1, lot_size));
}

int StatusAfterEntryFillFull(const config::SymbolConfig &sym, double prev_opened_qty,
                             double prev_opened_price, double fill_qty, double fill_px,
                             int lot_size) {
  (void)prev_opened_price;
  (void)fill_px;
  const int L = std::max(1, lot_size);
  const double total_qty = prev_opened_qty + fill_qty;
  if (total_qty <= 0.0) {
    return 0;
  }
  if (sym.entry_qty > 0.0) {
    const double target = sym.entry_qty;
    if (total_qty + 1e-9 >= target) {
      return 2;
    }
    const double remain = std::max(0.0, target - total_qty);
    if (EntryResidualCannotPlaceWholeLot(remain, L)) {
      return 2;
    }
    return 1;
  }
  // budget mode: single entry then monitor exit
  return 2;
}

int DefaultLotSizeForCode(const std::string &code) {
  if (code.size() >= 3 && (code[0] == 'H' || code[0] == 'h') && code[1] == 'K' && code[2] == '.') {
    return 100;
  }
  return 1;
}

}  // namespace strategy
}  // namespace mmtrade
