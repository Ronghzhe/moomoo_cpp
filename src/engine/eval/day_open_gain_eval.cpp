#include "engine/eval/day_open_gain_eval.hpp"

namespace mmtrade {
namespace engine {

namespace {

double RefPriceForDayOpen(const std::string &ref, double bid_1, double ask_1, bool has_bid_ask) {
  if (ref == "mid") {
    if (has_bid_ask) {
      return (bid_1 + ask_1) / 2.0;
    }
    return 0.0;
  }
  if (ref == "ask_1") {
    return has_bid_ask ? ask_1 : 0.0;
  }
  if (ref == "bid_1") {
    return has_bid_ask ? bid_1 : 0.0;
  }
  return 0.0;
}

}  // namespace

EvalResult EvaluateDayOpenGain(const DayOpenGainCondition &cond,
                               const DayOpenGainEvalContext &ctx) {
  EvalResult out;
  if (ctx.runtime == NULL) {
    out.handled = false;
    out.hit = false;
    out.reason = "missing_dependency";
    return out;
  }
  const double day_open = ctx.runtime->day_open;
  if (day_open <= 0.0) {
    out.handled = true;
    out.hit = false;
    out.reason = "day_open_not_ready";
    return out;
  }
  const double threshold = day_open * (1.0 + cond.gain_pct);
  const double lv = RefPriceForDayOpen(cond.ref, ctx.bid_1, ctx.ask_1, ctx.has_bid_ask);
  if (lv <= 0.0) {
    out.handled = true;
    out.hit = false;
    out.reason = "invalid_ref_price";
    return out;
  }
  out.handled = true;
  out.hit = lv > threshold;
  out.reason = "compared";
  out.has_left_value = true;
  out.left_value = lv;
  out.has_right_value = true;
  out.right_value = threshold;
  return out;
}

}  // namespace engine
}  // namespace mmtrade
