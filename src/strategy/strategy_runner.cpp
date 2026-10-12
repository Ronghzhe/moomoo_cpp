#include "strategy/strategy_runner.hpp"

namespace mmtrade {
namespace strategy {

SymbolJudgeOutcome RunSymbolJudge(const config::SymbolConfig &sym, const StrategyTickContext &tick,
                                  SymbolEvalRuntime *rt, LiveEvalCompute *live) {
  SymbolJudgeOutcome out;
  out.code = sym.code;
  if (rt == NULL) {
    return out;
  }
  const StrategyJudgeResult entry = JudgeEntryStrategies(sym, tick, rt, live);
  out.entry_hit = entry.hit;
  out.entry_rule_id = entry.hit_rule_id;
  const StrategyJudgeResult exit = JudgeExitStrategies(sym, tick, rt, live);
  out.exit_hit = exit.hit;
  out.exit_rule_id = exit.hit_rule_id;
  return out;
}

}  // namespace strategy
}  // namespace mmtrade
