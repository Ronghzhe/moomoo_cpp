#pragma once

#include "engine/condition_types.hpp"

namespace mmtrade {
namespace engine {

struct TrailingMa20CloseRuntime {
  double last_eval_ts;
  double snap_close;
  double snap_ma;
  std::string snap_ref_bar;
  std::string snap_time_key;
  std::string snap_note;
  std::string snap_side;
  double anchor_ts;

  TrailingMa20CloseRuntime()
      : last_eval_ts(0.0),
        snap_close(0.0),
        snap_ma(0.0),
        anchor_ts(0.0) {}
};

struct TrailingMa20RefComputeResult {
  bool ok;
  double cur_close;
  double ma_ref;
  std::string ref_bar;
  std::string bar_time_key;
  std::string note;

  TrailingMa20RefComputeResult() : ok(false), cur_close(0.0), ma_ref(0.0) {}
};

struct TrailingMa20CloseEvalContext {
  double now;
  std::string side;
  TrailingMa20CloseRuntime *runtime;
  bool has_compute;
  TrailingMa20RefComputeResult compute;

  TrailingMa20CloseEvalContext()
      : now(0.0), runtime(NULL), has_compute(false) {}
};

EvalResult EvaluateTrailingMa20Close(const TrailingMa20CloseCondition &cond,
                                     const TrailingMa20CloseEvalContext &ctx);

}  // namespace engine
}  // namespace mmtrade
