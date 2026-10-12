#pragma once

#include "config/strategy_types.hpp"
#include "strategy/tick_context.hpp"

#include <string>
#include <vector>

namespace mmtrade {
namespace strategy {

class LiveEvalCompute;
class SymbolEvalRuntime;

struct StrategyJudgeResult {
  bool hit;
  std::string hit_rule_id;
  std::vector<int> hit_condition_indices;

  StrategyJudgeResult() : hit(false) {}
};

StrategyJudgeResult JudgeEntryStrategies(const config::SymbolConfig &sym,
                                         const StrategyTickContext &tick,
                                         SymbolEvalRuntime *rt, LiveEvalCompute *live);

StrategyJudgeResult JudgeExitStrategies(const config::SymbolConfig &sym,
                                        const StrategyTickContext &tick,
                                        SymbolEvalRuntime *rt, LiveEvalCompute *live);

}  // namespace strategy
}  // namespace mmtrade
