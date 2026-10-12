#pragma once

#include "config/strategy_types.hpp"
#include "strategy/tick_context.hpp"

namespace mmtrade {
namespace strategy {

StrategyTickContext BuildTickContext(const config::SymbolConfig &sym, const BookTick &book,
                                     double now, const std::string &code_full);

}  // namespace strategy
}  // namespace mmtrade
