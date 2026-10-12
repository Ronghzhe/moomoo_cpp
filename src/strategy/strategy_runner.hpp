#pragma once

#include "config/strategy_types.hpp"
#include "strategy/strategy_judge.hpp"
#include "strategy/tick_context.hpp"

#include <string>

namespace mmtrade {
namespace strategy {

struct SymbolJudgeOutcome {
  std::string code;
  bool entry_hit;
  std::string entry_rule_id;
  bool exit_hit;
  std::string exit_rule_id;

  SymbolJudgeOutcome() : entry_hit(false), exit_hit(false) {}
};

SymbolJudgeOutcome RunSymbolJudge(const config::SymbolConfig &sym, const StrategyTickContext &tick,
                                  SymbolEvalRuntime *rt, LiveEvalCompute *live);

}  // namespace strategy
}  // namespace mmtrade
