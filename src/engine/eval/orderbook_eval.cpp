#include "engine/eval/orderbook_eval.hpp"

#include "engine/compare.hpp"

#include <algorithm>
#include <cctype>

namespace mmtrade {
namespace engine {

namespace {

std::string TrimCopy(const std::string &s) {
  size_t start = 0;
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
    ++start;
  }
  size_t end = s.size();
  while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
    --end;
  }
  return s.substr(start, end - start);
}

bool FieldHasValue(const FieldMap &fields, const std::string &field, double *out) {
  FieldMap::const_iterator it = fields.find(field);
  if (it == fields.end()) {
    return false;
  }
  if (out != NULL) {
    *out = it->second;
  }
  return true;
}

}  // namespace

EvalResult EvaluateOrderbook(const PriceCondition &cond, const FieldMap &fields) {
  EvalResult out;
  out.handled = true;
  out.field = cond.field;
  const std::string op = TrimCopy(cond.op);
  out.op = op;

  if (op.empty()) {
    out.hit = true;
    out.reason = "op_empty_skip_compare";
    return out;
  }

  double left = 0.0;
  if (!FieldHasValue(fields, cond.field, &left) || !cond.has_value) {
    out.hit = false;
    out.reason = "missing_lv_or_thresh";
    if (FieldHasValue(fields, cond.field, &left)) {
      out.has_left_value = true;
      out.left_value = left;
    }
    if (cond.has_value) {
      out.has_right_value = true;
      out.right_value = cond.value;
    }
    return out;
  }

  out.has_left_value = true;
  out.left_value = left;
  out.has_right_value = true;
  out.right_value = cond.value;
  out.hit = CompareFloat(left, op, cond.value);
  out.reason = "compared";
  return out;
}

EvalResult EvaluatePnl(const PnlCondition &cond, const PnlEvalContext &ctx) {
  EvalResult out;
  out.handled = true;
  out.op = cond.op;
  out.has_right_value = true;
  out.right_value = cond.value;

  if (!ctx.has_gain_fraction) {
    out.hit = false;
    out.reason = "pnl_unavailable";
    return out;
  }

  out.has_left_value = true;
  out.left_value = ctx.gain_fraction;
  out.hit = CompareFloat(ctx.gain_fraction, cond.op, cond.value);
  out.reason = "compared";
  return out;
}

EvalResult EvaluateKlineClose(const KlineCloseCondition &cond, const KlineCloseEvalContext &ctx) {
  EvalResult out;
  if (ctx.runtime == NULL || !ctx.has_fetch || !ctx.has_gate) {
    out.handled = false;
    out.hit = false;
    out.reason = "missing_dependency";
    return out;
  }

  KlineCloseRuntime &rt = *ctx.runtime;
  const bool overdue = rt.last_eval_ts <= 0.0 || (ctx.now - rt.last_eval_ts >= cond.poll_sec);
  if (overdue) {
    if (ctx.fetch.ok) {
      rt.snap_close = ctx.fetch.close;
    } else {
      rt.snap_close = 0.0;
    }
    rt.snap_ref_bar = ctx.fetch.ref_bar;
    rt.snap_time_key = ctx.fetch.time_key;
    rt.snap_note = ctx.fetch.note;
    rt.last_eval_ts = ctx.now;
  }

  const bool gate_ok = ctx.gate_ok;
  const std::string op = TrimCopy(cond.op);
  bool ok = false;
  if (!gate_ok) {
    ok = false;
  } else if (op.empty()) {
    ok = true;
  } else if (!cond.has_value) {
    ok = false;
  } else {
    ok = CompareFloat(rt.snap_close, op, cond.value);
  }

  out.handled = true;
  out.hit = ok;
  out.reason = "kline_close";
  out.has_left_value = true;
  out.left_value = rt.snap_close;
  if (cond.has_value) {
    out.has_right_value = true;
    out.right_value = cond.value;
  }
  out.op = op.empty() ? "<empty>" : op;
  return out;
}

}  // namespace engine
}  // namespace mmtrade
