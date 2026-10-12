#pragma once

#include "engine/condition_types.hpp"

namespace mmtrade {
namespace engine {

struct VolumeRuntime {
  double last_eval_ts;
  bool cached_hit;
  double snap_ma;
  double snap_vol;
  double snap_thresh;
  std::string snap_ref_bar;

  VolumeRuntime()
      : last_eval_ts(0.0), cached_hit(false), snap_ma(0.0), snap_vol(0.0), snap_thresh(0.0) {}
};

struct VolumeComputeResult {
  bool hit;
  double ma;
  double vol;
  double thresh;
  std::string note;
  std::string ref_bar;

  VolumeComputeResult() : hit(false), ma(0.0), vol(0.0), thresh(0.0) {}
};

struct VolumeEvalContext {
  double now;
  VolumeRuntime *runtime;
  bool has_compute;
  VolumeComputeResult compute;

  VolumeEvalContext() : now(0.0), runtime(NULL), has_compute(false) {}
};

EvalResult EvaluateVolume(const VolumeEntryWhen &cond, const VolumeEvalContext &ctx);

}  // namespace engine
}  // namespace mmtrade
