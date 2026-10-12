#include "engine/eval/trailing_ma20_realtime_eval.hpp"

#include "engine/compare.hpp"

#include <algorithm>
#include <cctype>

namespace mmtrade {
namespace engine {

namespace {

double ResolvePriceField(const TrailingMa20RtEvalContext &ctx, const std::string &price_field) {
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

std::string NormalizeOp(const std::string &raw) {
  size_t start = 0;
  while (start < raw.size() && std::isspace(static_cast<unsigned char>(raw[start]))) {
    ++start;
  }
  size_t end = raw.size();
  while (end > start && std::isspace(static_cast<unsigned char>(raw[end - 1]))) {
    --end;
  }
  return raw.substr(start, end - start);
}

}  // namespace

EvalResult EvaluateTrailingMa20Realtime(const TrailingMa20RealtimeCondition &cond,
                                        const TrailingMa20RtEvalContext &ctx) {
  EvalResult out;
  if (ctx.runtime == NULL || !ctx.has_compute) {
    out.handled = false;
    out.hit = false;
    out.reason = "missing_dependency";
    return out;
  }

  TrailingMa20RtRuntime &rt = *ctx.runtime;
  const std::string side_norm =
      NormalizeSide(ctx.use_pos_side ? ctx.pos_side : ctx.side);
  const double px = ResolvePriceField(ctx, cond.price_field);
  if (px <= 0.0) {
    rt.last_eval_ts = ctx.now;
    rt.snap_note = "invalid_price_field=" + cond.price_field;
    rt.snap_side = side_norm;
    rt.snap_px = 0.0;
    rt.snap_ma = 0.0;
    rt.snap_ref_bar = "-";
    rt.snap_time_key = "";
  } else {
    rt.last_eval_ts = ctx.now;
    rt.snap_side = side_norm;
    rt.snap_px = px;
    rt.snap_ma = ctx.compute.ok ? ctx.compute.ma_ref : 0.0;
    rt.snap_ref_bar = ctx.compute.ref_bar;
    rt.snap_time_key = ctx.compute.bar_time_key;
    rt.snap_note = ctx.compute.note;
  }

  std::string op_eff = NormalizeOp(cond.op);
  if (op_eff.empty()) {
    op_eff = (side_norm == "BUY") ? "<" : ">";
  }
  const bool ok = rt.snap_px > 0.0 && rt.snap_ma > 0.0 &&
                  CompareFloat(rt.snap_px, op_eff, rt.snap_ma);

  out.handled = true;
  out.hit = ok;
  out.reason = "trailing_ma20_realtime";
  out.op = op_eff;
  out.has_left_value = true;
  out.left_value = rt.snap_px;
  out.has_right_value = true;
  out.right_value = rt.snap_ma;
  return out;
}

}  // namespace engine
}  // namespace mmtrade
