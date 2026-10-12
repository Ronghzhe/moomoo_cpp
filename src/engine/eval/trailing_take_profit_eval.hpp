#pragma once

#include "engine/condition_types.hpp"

namespace mmtrade {
namespace engine {

struct TrailingTpRuntime {
  double last_eval_ts;
  double snap_px;
  double snap_extreme;
  double snap_trigger;
  std::string snap_side;
  std::string snap_ref_bar;
  std::string snap_note;
  std::string anchor_source;
  double anchor_ts;

  TrailingTpRuntime()
      : last_eval_ts(0.0),
        snap_px(0.0),
        snap_extreme(0.0),
        snap_trigger(0.0),
        anchor_ts(0.0) {}
};

struct TrailingTpComputeResult {
  bool ok;
  double extreme;
  double trigger;
  double current_px;
  std::string ref_bar;
  std::string note;

  TrailingTpComputeResult() : ok(false), extreme(0.0), trigger(0.0), current_px(0.0) {}
};

struct TrailingTpEvalContext {
  double now;
  FieldMap fields;
  double bid_1;
  double ask_1;
  bool has_bid_ask;
  std::string side;
  TrailingTpRuntime *runtime;
  bool has_compute;
  TrailingTpComputeResult compute;

  TrailingTpEvalContext()
      : now(0.0),
        bid_1(0.0),
        ask_1(0.0),
        has_bid_ask(false),
        runtime(NULL),
        has_compute(false) {}
};

EvalResult EvaluateTrailingTakeProfit(const TrailingTakeProfitCondition &cond,
                                      const TrailingTpEvalContext &ctx);

}  // namespace engine
}  // namespace mmtrade
