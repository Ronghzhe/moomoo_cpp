#include "engine/eval/trailing_take_profit_eval.hpp"

#include "engine/compare.hpp"

#include <algorithm>
#include <cctype>

namespace mmtrade {
namespace engine {

namespace {

double ResolvePriceField(const TrailingTpEvalContext &ctx, const std::string &price_field) {
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

std::string NormalizeSide(const std::string &side) {
  std::string s = side;
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return s;
}

}  // namespace

EvalResult EvaluateTrailingTakeProfit(const TrailingTakeProfitCondition &cond,
                                      const TrailingTpEvalContext &ctx) {
  EvalResult out;
  if (ctx.runtime == NULL || !ctx.has_compute) {
    out.handled = false;
    out.hit = false;
    out.reason = "missing_dependency";
    return out;
  }

  TrailingTpRuntime &rt = *ctx.runtime;
  const double px = ResolvePriceField(ctx, cond.price_field);
  const std::string side_norm = NormalizeSide(ctx.side);

  if (px <= 0.0) {
    rt.last_eval_ts = ctx.now;
    rt.snap_note = "invalid_price_field=" + cond.price_field;
    rt.snap_ref_bar = "-";
    rt.snap_side = side_norm;
    rt.snap_px = 0.0;
    rt.snap_extreme = 0.0;
    rt.snap_trigger = 0.0;
  } else if (ctx.compute.ok) {
    rt.last_eval_ts = ctx.now;
    rt.snap_side = side_norm;
    rt.snap_px = ctx.compute.current_px;
    rt.snap_extreme = ctx.compute.extreme;
    rt.snap_trigger = ctx.compute.trigger;
    rt.snap_ref_bar = ctx.compute.ref_bar;
    rt.snap_note = ctx.compute.note;
  } else {
    rt.last_eval_ts = ctx.now;
    rt.snap_side = side_norm;
    rt.snap_px = px;
    rt.snap_extreme = 0.0;
    rt.snap_trigger = 0.0;
    rt.snap_ref_bar = ctx.compute.ref_bar;
    rt.snap_note = ctx.compute.note;
  }

  std::string op_eff = cond.op;
  size_t start = 0;
  while (start < op_eff.size() && std::isspace(static_cast<unsigned char>(op_eff[start]))) {
    ++start;
  }
  size_t end = op_eff.size();
  while (end > start && std::isspace(static_cast<unsigned char>(op_eff[end - 1]))) {
    --end;
  }
  op_eff = op_eff.substr(start, end - start);
  if (op_eff.empty()) {
    op_eff = (side_norm == "BUY") ? "<" : ">";
  }

  const bool ok = rt.snap_px > 0.0 && rt.snap_trigger > 0.0 &&
                  CompareFloat(rt.snap_px, op_eff, rt.snap_trigger);

  out.handled = true;
  out.hit = ok;
  out.reason = "trailing_take_profit";
  out.op = op_eff;
  out.has_left_value = true;
  out.left_value = rt.snap_px;
  out.has_right_value = true;
  out.right_value = rt.snap_trigger;
  return out;
}

}  // namespace engine
}  // namespace mmtrade
