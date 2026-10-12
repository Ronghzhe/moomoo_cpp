#pragma once

#include "engine/condition_types.hpp"

namespace mmtrade {
namespace engine {

struct DayOpenGainRuntime {
  double day_open;

  DayOpenGainRuntime() : day_open(0.0) {}
};

struct DayOpenGainEvalContext {
  DayOpenGainRuntime *runtime;
  double bid_1;
  double ask_1;
  bool has_bid_ask;

  DayOpenGainEvalContext() : runtime(NULL), bid_1(0.0), ask_1(0.0), has_bid_ask(false) {}
};

EvalResult EvaluateDayOpenGain(const DayOpenGainCondition &cond,
                               const DayOpenGainEvalContext &ctx);

}  // namespace engine
}  // namespace mmtrade
