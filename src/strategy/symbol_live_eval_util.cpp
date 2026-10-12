#include "strategy/live_eval_inject.hpp"

namespace mmtrade {
namespace strategy {

namespace {

bool ConditionNeedsLiveEval(config::ConditionKind kind) {
  switch (kind) {
    case config::ConditionKind::kVolume:
    case config::ConditionKind::kKlineClose:
    case config::ConditionKind::kKlineCandle:
    case config::ConditionKind::kTrailingMa20Close:
    case config::ConditionKind::kTrailingMa20Realtime:
      return true;
    default:
      return false;
  }
}

bool RuleListNeedsLiveEval(const std::vector<config::EntryStrategyRule> &rules) {
  for (size_t i = 0; i < rules.size(); ++i) {
    if (!rules[i].enabled) {
      continue;
    }
    for (size_t j = 0; j < rules[i].conditions.size(); ++j) {
      if (ConditionNeedsLiveEval(rules[i].conditions[j].kind)) {
        return true;
      }
    }
  }
  return false;
}

bool ExitRulesNeedLiveEval(const std::vector<config::ExitStrategyRule> &rules) {
  for (size_t i = 0; i < rules.size(); ++i) {
    if (!rules[i].enabled) {
      continue;
    }
    for (size_t j = 0; j < rules[i].conditions.size(); ++j) {
      if (ConditionNeedsLiveEval(rules[i].conditions[j].kind)) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

bool SymbolNeedsLiveEval(const config::SymbolConfig &sym) {
  return RuleListNeedsLiveEval(sym.entry_strategies) ||
         ExitRulesNeedLiveEval(sym.exit_strategies);
}

}  // namespace strategy
}  // namespace mmtrade
