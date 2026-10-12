#include "engine/eval/volume_eval.hpp"

namespace mmtrade {
namespace engine {

EvalResult EvaluateVolume(const VolumeEntryWhen &cond, const VolumeEvalContext &ctx) {
  EvalResult out;
  if (ctx.runtime == NULL || !ctx.has_compute) {
    out.handled = false;
    out.hit = false;
    out.reason = "missing_dependency";
    return out;
  }

  VolumeRuntime &rt = *ctx.runtime;
  const bool overdue =
      rt.last_eval_ts <= 0.0 || (ctx.now - rt.last_eval_ts >= cond.poll_sec);
  if (overdue) {
    rt.last_eval_ts = ctx.now;
    rt.cached_hit = ctx.compute.hit;
    rt.snap_ma = ctx.compute.ma;
    rt.snap_vol = ctx.compute.vol;
    rt.snap_thresh = ctx.compute.thresh;
    rt.snap_ref_bar = ctx.compute.ref_bar;
  }

  out.handled = true;
  out.hit = rt.cached_hit;
  out.reason = "volume";
  out.has_left_value = true;
  out.left_value = rt.snap_vol;
  out.has_right_value = true;
  out.right_value = rt.snap_thresh;
  return out;
}

}  // namespace engine
}  // namespace mmtrade
