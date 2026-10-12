#include "engine/eval/before_session_close_eval.hpp"

namespace mmtrade {
namespace engine {

EvalResult EvaluateBeforeSessionClose(const BeforeSessionCloseCondition &cond,
                                      const BeforeSessionCloseEvalContext &ctx) {
  EvalResult out;
  if (!ctx.has_local_now) {
    out.handled = false;
    out.hit = false;
    out.reason = "missing_dependency";
    return out;
  }
  if (cond.session != "rth") {
    out.handled = true;
    out.hit = false;
    out.reason = "unsupported_session";
    return out;
  }
  const SessionCloseWindowResult win =
      SessionCloseWindowRthLocal(ctx.local_now, cond.minutes_before);
  out.handled = true;
  out.hit = win.ok;
  out.reason = win.note;
  return out;
}

}  // namespace engine
}  // namespace mmtrade
