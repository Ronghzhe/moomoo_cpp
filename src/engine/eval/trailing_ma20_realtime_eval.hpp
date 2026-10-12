#pragma once

#include "engine/condition_types.hpp"
#include "engine/eval/trailing_ma20_close_eval.hpp"

namespace mmtrade {
namespace engine {

struct TrailingMa20RtRuntime {
  double last_eval_ts;
  double snap_px;
  double snap_ma;
  std::string snap_ref_bar;
  std::string snap_time_key;
  std::string snap_note;
  std::string snap_side;

  TrailingMa20RtRuntime()
      : last_eval_ts(0.0), snap_px(0.0), snap_ma(0.0) {}
};

struct TrailingMa20RtEvalContext {
  double now;
  std::string side;
  std::string pos_side;
  bool use_pos_side;
  FieldMap fields;
  double bid_1;
  double ask_1;
  bool has_bid_ask;
  TrailingMa20RtRuntime *runtime;
  bool has_compute;
  TrailingMa20RefComputeResult compute;

  TrailingMa20RtEvalContext()
      : now(0.0),
        use_pos_side(false),
        bid_1(0.0),
        ask_1(0.0),
        has_bid_ask(false),
        runtime(NULL),
        has_compute(false) {}
};

EvalResult EvaluateTrailingMa20Realtime(const TrailingMa20RealtimeCondition &cond,
                                        const TrailingMa20RtEvalContext &ctx);

}  // namespace engine
}  // namespace mmtrade
