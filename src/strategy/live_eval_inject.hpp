#pragma once

#include "config/strategy_types.hpp"
#include "engine/registry.hpp"
#include "strategy/tick_context.hpp"

namespace mmtrade {
namespace strategy {

class LiveEvalCompute;

bool SymbolNeedsLiveEval(const config::SymbolConfig &sym);

void InjectLiveEntryEval(const config::Condition &cond, LiveEvalCompute *live,
                         const StrategyTickContext &tick, engine::EntryEvalBundle *bundle);

void InjectLiveExitEval(const config::Condition &cond, LiveEvalCompute *live,
                        const StrategyTickContext &tick, engine::ExitEvalBundle *bundle);

}  // namespace strategy
}  // namespace mmtrade
