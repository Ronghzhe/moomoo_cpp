#pragma once

#include "config/strategy_types.hpp"

#include <string>

namespace mmtrade {
namespace strategy {

struct EntrySizingResult {
  double qty;
  bool ok;
  std::string note;

  EntrySizingResult() : qty(0.0), ok(false) {}
};

EntrySizingResult SizeQtyFromBudget(double budget, double price, int lot_size);

double ResolveEntryQtyForPlace(const config::SymbolConfig &sym, double price, int lot_size,
                                 std::string *note);

double EntryQtyRemain(const config::SymbolConfig &sym);

bool EntryResidualCannotPlaceWholeLot(double remain, int lot_size);

int StatusAfterEntryFillFull(const config::SymbolConfig &sym, double prev_opened_qty,
                             double prev_opened_price, double fill_qty, double fill_px,
                             int lot_size);

int DefaultLotSizeForCode(const std::string &code);

}  // namespace strategy
}  // namespace mmtrade
