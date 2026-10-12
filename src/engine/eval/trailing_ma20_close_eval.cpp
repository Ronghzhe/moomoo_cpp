#include "engine/eval/trailing_ma20_close_eval.hpp"

#include "engine/compare.hpp"

#include <algorithm>
#include <cctype>

namespace mmtrade {
namespace engine {

namespace {

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

EvalResult EvaluateTrailingMa20Close(const TrailingMa20CloseCondition &cond,
                                     const TrailingMa20CloseEvalContext &ctx) {
  EvalResult out;
  if (ctx.runtime == NULL || !ctx.has_compute) {
    out.handled = false;
    out.hit = false;
    out.reason = "missing_dependency";
    return out;
  }

  TrailingMa20CloseRuntime &rt = *ctx.runtime;
  const bool overdue = rt.last_eval_ts <= 0.0 || (ctx.now - rt.last_eval_ts >= cond.poll_sec);
  if (overdue) {
    rt.last_eval_ts = ctx.now;
    rt.snap_side = NormalizeSide(ctx.side);
    rt.snap_close = ctx.compute.ok ? ctx.compute.cur_close : 0.0;
    rt.snap_ma = ctx.compute.ok ? ctx.compute.ma_ref : 0.0;
    rt.snap_ref_bar = ctx.compute.ref_bar;
    rt.snap_time_key = ctx.compute.bar_time_key;
    rt.snap_note = ctx.compute.note;
  }

  const std::string side_norm = NormalizeSide(ctx.side);
  std::string op_eff = NormalizeOp(cond.op);
  if (op_eff.empty()) {
    op_eff = (side_norm == "BUY") ? "<" : ">";
  }
  const double ma_raw = rt.snap_ma;
  const double relax = cond.ma_ref_relax;
  const double ma_thr = ma_raw * (1.0 - relax);
  const bool ok = rt.snap_close > 0.0 && ma_raw > 0.0 && ma_thr > 0.0 &&
                  CompareFloat(rt.snap_close, op_eff, ma_thr);

  out.handled = true;
  out.hit = ok;
  out.reason = "trailing_ma20_close";
  out.op = op_eff;
  out.has_left_value = true;
  out.left_value = rt.snap_close;
  out.has_right_value = true;
  out.right_value = ma_thr;
  return out;
}

}  // namespace engine
}  // namespace mmtrade
