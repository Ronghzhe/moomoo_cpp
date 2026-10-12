#pragma once

#include "engine/condition_types.hpp"
#include "engine/session_close_util.hpp"

namespace mmtrade {
namespace engine {

struct BeforeSessionCloseEvalContext {
  std::string code;
  bool has_local_now;
  LocalDateTime local_now;

  BeforeSessionCloseEvalContext() : has_local_now(false) {}
};

EvalResult EvaluateBeforeSessionClose(const BeforeSessionCloseCondition &cond,
                                      const BeforeSessionCloseEvalContext &ctx);

}  // namespace engine
}  // namespace mmtrade
