#pragma once

#include "config/strategy_types.hpp"
#include "engine/registry.hpp"
#include "strategy/symbol_runtime.hpp"
#include "strategy/tick_context.hpp"

namespace mmtrade {
namespace strategy {

class LiveEvalCompute;

void BuildEntryEvalBundle(const config::Condition &cond, const StrategyTickContext &tick,
                          SymbolEvalRuntime *rt, const std::string &entry_side,
                          const std::string &rule_id, int cond_idx, LiveEvalCompute *live,
                          engine::EntryEvalBundle *out);

void BuildExitEvalBundle(const config::Condition &cond, const config::SymbolConfig &sym,
                         const StrategyTickContext &tick, SymbolEvalRuntime *rt,
                         const std::string &exit_side, const std::string &rule_id, int cond_idx,
                         LiveEvalCompute *live, engine::ExitEvalBundle *out);

}  // namespace strategy
}  // namespace mmtrade
