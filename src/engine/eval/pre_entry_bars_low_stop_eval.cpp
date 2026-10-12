#include "engine/eval/pre_entry_bars_low_stop_eval.hpp"

#include <sstream>

namespace mmtrade {
namespace engine {

namespace {

double ResolvePriceField(const PreEntryLowStopEvalContext &ctx, const std::string &price_field) {
  if (price_field == "mid") {
    if (ctx.has_bid_ask) {
      return (ctx.bid_1 + ctx.ask_1) / 2.0;
    }
    return 0.0;
  }
  FieldMap::const_iterator it = ctx.fields.find(price_field);
  if (it != ctx.fields.end()) {
    return it->second;
  }
  if (price_field == "bid_1" && ctx.has_bid_ask) {
    return ctx.bid_1;
  }
  if (price_field == "ask_1" && ctx.has_bid_ask) {
    return ctx.ask_1;
  }
  return 0.0;
}

}  // namespace

EvalResult EvaluatePreEntryBarsLowStop(const PreEntryBarsLowStopCondition &cond,
                                       const PreEntryLowStopEvalContext &ctx) {
  EvalResult out;
  if (ctx.runtime == NULL || !ctx.has_pos_side) {
    out.handled = false;
    out.hit = false;
    out.reason = "missing_dependency";
    return out;
  }

  std::ostringstream key;
  key << ctx.rule_id << ":" << ctx.cond_idx;
  const std::string k = key.str();
  const std::map<std::string, double> &levels = ctx.runtime->stop_levels;
  std::map<std::string, double>::const_iterator it = levels.find(k);
  const double stop = (it == levels.end()) ? 0.0 : it->second;
  if (stop <= 0.0) {
    out.handled = true;
    out.hit = false;
    out.reason = "stop_not_ready";
    return out;
  }

  const double px = ResolvePriceField(ctx, cond.price_field);
  if (px <= 0.0) {
    out.handled = true;
    out.hit = false;
    out.reason = "invalid_price";
    return out;
  }

  const std::string &pos_dir = ctx.pos_side;
  bool hit = false;
  std::string op;
  if (pos_dir == "BUY") {
    hit = px <= stop;
    op = "<=";
  } else {
    hit = px >= stop;
    op = ">=";
  }

  out.handled = true;
  out.hit = hit;
  out.reason = "compared";
  out.op = op;
  out.has_left_value = true;
  out.left_value = px;
  out.has_right_value = true;
  out.right_value = stop;
  return out;
}

}  // namespace engine
}  // namespace mmtrade
