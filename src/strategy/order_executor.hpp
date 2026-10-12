#pragma once

#include "config/strategy_types.hpp"
#include "strategy/tick_context.hpp"

#include <string>

namespace mmtrade {
namespace strategy {

double ComputeSubmitPrice(const config::SymbolConfig &sym, const BookTick &book,
                          const std::string &side);

double ComputeSubmitPriceForAttempt(const config::SymbolConfig &sym, const BookTick &book,
                                    const std::string &side, int attempt_index,
                                    bool market_fallback);

double ResolveEntryQty(const config::SymbolConfig &sym);

double ResolveExitQty(const config::SymbolConfig &sym);

}  // namespace strategy
}  // namespace mmtrade
