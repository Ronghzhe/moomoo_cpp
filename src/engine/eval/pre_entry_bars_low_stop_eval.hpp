#pragma once

#include "engine/condition_types.hpp"

#include <map>
#include <string>

namespace mmtrade {
namespace engine {

struct PreEntryLowStopRuntime {
  std::map<std::string, double> stop_levels;

  PreEntryLowStopRuntime() {}
};

struct PreEntryLowStopEvalContext {
  PreEntryLowStopRuntime *runtime;
  FieldMap fields;
  double bid_1;
  double ask_1;
  bool has_bid_ask;
  std::string side;
  std::string pos_side;
  bool has_pos_side;
  std::string rule_id;
  int cond_idx;

  PreEntryLowStopEvalContext()
      : runtime(NULL),
        bid_1(0.0),
        ask_1(0.0),
        has_bid_ask(false),
        has_pos_side(false),
        cond_idx(-1) {}
};

EvalResult EvaluatePreEntryBarsLowStop(const PreEntryBarsLowStopCondition &cond,
                                       const PreEntryLowStopEvalContext &ctx);

}  // namespace engine
}  // namespace mmtrade
