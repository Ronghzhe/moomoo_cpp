#include "engine/eval/kline_candle_eval.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace mmtrade {
namespace engine {

namespace {

std::string TrimLower(const std::string &s) {
  size_t start = 0;
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
    ++start;
  }
  size_t end = s.size();
  while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
    --end;
  }
  std::string out = s.substr(start, end - start);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

std::string BuildOnceKey(const KlineCandleEvalContext &ctx, const KlineCandleCondition &cond,
                         const std::string &cref_norm) {
  std::ostringstream oss;
  oss << ctx.rule_id << ":" << ctx.cond_idx << ":" << cond.kl_type << ":" << cond.candle << ":"
      << cond.after_open_minutes << ":" << cref_norm;
  return oss.str();
}

}  // namespace

EvalResult EvaluateKlineCandle(const KlineCandleCondition &cond,
                               const KlineCandleEvalContext &ctx) {
  EvalResult out;
  if (ctx.runtime == NULL || !ctx.has_fetch || !ctx.has_gate) {
    out.handled = false;
    out.hit = false;
    out.reason = "missing_dependency";
    return out;
  }

  KlineCandleRuntime &rt = *ctx.runtime;
  std::string eval_mode = TrimLower(cond.eval_mode);
  if (eval_mode != "repeat" && eval_mode != "once") {
    eval_mode = "repeat";
  }
  const std::string cref_norm = NormalizeKlineCandleRef(cond.candle_ref);
  const std::string once_key = BuildOnceKey(ctx, cond, cref_norm);

  if (eval_mode == "once") {
    std::map<std::string, bool>::const_iterator done_it = rt.once_done.find(once_key);
    if (done_it != rt.once_done.end() && done_it->second) {
      std::map<std::string, bool>::const_iterator ok_it = rt.once_ok.find(once_key);
      out.handled = true;
      out.hit = (ok_it != rt.once_ok.end() && ok_it->second);
      out.reason = "kline_candle";
      return out;
    }
  }

  bool ok_fetch = true;
  const bool overdue =
      rt.last_eval_ts <= 0.0 || (ctx.now - rt.last_eval_ts >= cond.poll_sec);
  if (overdue) {
    ok_fetch = ctx.fetch.ok;
    rt.last_eval_ts = ctx.now;
    rt.snap_open = ok_fetch ? ctx.fetch.open : 0.0;
    rt.snap_close = ok_fetch ? ctx.fetch.close : 0.0;
    rt.snap_ref_bar = ctx.fetch.ref_bar;
    rt.snap_time_key = ctx.fetch.time_key;
    rt.snap_note = ctx.fetch.note;
  }

  const bool gate_ok = ctx.gate_ok;
  const double o = rt.snap_open;
  const double c = rt.snap_close;
  const KlineCandleShapeResult shape =
      EvaluateKlineCandleShape(cond, c, o, rt.prev_close);
  rt.snap_ref_px = shape.ref_px;

  const bool has_bar = !rt.snap_time_key.empty();
  bool ok = false;
  if (has_bar && ok_fetch && gate_ok) {
    ok = shape.hit;
  }

  if (eval_mode == "once" && gate_ok) {
    rt.once_done[once_key] = true;
    rt.once_ok[once_key] = ok;
  }

  out.handled = true;
  out.hit = ok;
  out.reason = "kline_candle";
  out.field = shape.shape_rule;
  return out;
}

}  // namespace engine
}  // namespace mmtrade
