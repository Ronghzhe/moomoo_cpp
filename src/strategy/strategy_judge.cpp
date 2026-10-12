#include "strategy/strategy_judge.hpp"

#include "engine/registry.hpp"
#include "strategy/eval_bundle_factory.hpp"
#include "strategy/rule_logic.hpp"
#include "strategy/symbol_runtime.hpp"

namespace mmtrade {
namespace strategy {

namespace {

bool EvalConditionHit(const engine::EvalResult &result) {
  return result.handled && result.hit;
}

StrategyJudgeResult JudgeRuleListEntry(const config::SymbolConfig &sym,
                                       const StrategyTickContext &tick, SymbolEvalRuntime *rt,
                                       LiveEvalCompute *live, const std::string &top_logic) {
  StrategyJudgeResult out;
  std::vector<bool> rule_hits;
  for (size_t i = 0; i < sym.entry_strategies.size(); ++i) {
    const config::EntryStrategyRule &rule = sym.entry_strategies[i];
    if (!rule.enabled) {
      continue;
    }
    std::vector<bool> cond_hits;
    for (size_t j = 0; j < rule.conditions.size(); ++j) {
      engine::EntryEvalBundle bundle;
      BuildEntryEvalBundle(rule.conditions[j], tick, rt, sym.entry_side, rule.rule_id,
                           static_cast<int>(j), live, &bundle);
      cond_hits.push_back(EvalConditionHit(engine::EvaluateEntryCondition(bundle)));
    }
    const bool rh = BoolMerge(cond_hits, rule.logic);
    rule_hits.push_back(rh);
    if (rh && out.hit_rule_id.empty()) {
      out.hit_rule_id = rule.rule_id;
      for (size_t j = 0; j < cond_hits.size(); ++j) {
        if (cond_hits[j]) {
          out.hit_condition_indices.push_back(static_cast<int>(j));
        }
      }
    }
    if (top_logic == "any" && rh) {
      out.hit = true;
      return out;
    }
  }
  out.hit = BoolMerge(rule_hits, top_logic);
  if (!out.hit) {
    out.hit_rule_id.clear();
    out.hit_condition_indices.clear();
  }
  return out;
}

StrategyJudgeResult JudgeRuleListExit(const config::SymbolConfig &sym,
                                      const StrategyTickContext &tick, SymbolEvalRuntime *rt,
                                      LiveEvalCompute *live, const std::string &top_logic) {
  StrategyJudgeResult out;
  std::vector<bool> rule_hits;
  for (size_t i = 0; i < sym.exit_strategies.size(); ++i) {
    const config::ExitStrategyRule &rule = sym.exit_strategies[i];
    if (!rule.enabled) {
      continue;
    }
    std::vector<bool> cond_hits;
    for (size_t j = 0; j < rule.conditions.size(); ++j) {
      engine::ExitEvalBundle bundle;
      BuildExitEvalBundle(rule.conditions[j], sym, tick, rt, rule.side, rule.rule_id,
                          static_cast<int>(j), live, &bundle);
      cond_hits.push_back(EvalConditionHit(engine::EvaluateExitCondition(bundle)));
    }
    const bool rh = BoolMerge(cond_hits, rule.logic);
    rule_hits.push_back(rh);
    if (rh && out.hit_rule_id.empty()) {
      out.hit_rule_id = rule.rule_id;
      for (size_t j = 0; j < cond_hits.size(); ++j) {
        if (cond_hits[j]) {
          out.hit_condition_indices.push_back(static_cast<int>(j));
        }
      }
    }
    if (top_logic == "any" && rh) {
      out.hit = true;
      return out;
    }
  }
  out.hit = BoolMerge(rule_hits, top_logic);
  if (!out.hit) {
    out.hit_rule_id.clear();
    out.hit_condition_indices.clear();
  }
  return out;
}

}  // namespace

StrategyJudgeResult JudgeEntryStrategies(const config::SymbolConfig &sym,
                                         const StrategyTickContext &tick,
                                         SymbolEvalRuntime *rt, LiveEvalCompute *live) {
  if (rt == NULL) {
    return StrategyJudgeResult();
  }
  const std::string top_logic = sym.entry_strategy_logic.empty() ? "any" : sym.entry_strategy_logic;
  return JudgeRuleListEntry(sym, tick, rt, live, top_logic);
}

StrategyJudgeResult JudgeExitStrategies(const config::SymbolConfig &sym,
                                        const StrategyTickContext &tick,
                                        SymbolEvalRuntime *rt, LiveEvalCompute *live) {
  if (rt == NULL || !sym.exit.enabled) {
    return StrategyJudgeResult();
  }
  const std::string top_logic = sym.exit_strategy_logic.empty() ? "any" : sym.exit_strategy_logic;
  return JudgeRuleListExit(sym, tick, rt, live, top_logic);
}

}  // namespace strategy
}  // namespace mmtrade
