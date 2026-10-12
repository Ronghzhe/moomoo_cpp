#pragma once

#include "engine/condition_types.hpp"

namespace mmtrade {
namespace engine {

EvalResult EvaluateOrderbook(const PriceCondition &cond, const FieldMap &fields);

struct PnlEvalContext {
  bool has_gain_fraction;
  double gain_fraction;

  PnlEvalContext() : has_gain_fraction(false), gain_fraction(0.0) {}
};

EvalResult EvaluatePnl(const PnlCondition &cond, const PnlEvalContext &ctx);

struct KlineCloseRuntime {
  double last_eval_ts;
  double snap_close;
  std::string snap_ref_bar;
  std::string snap_time_key;
  std::string snap_note;

  KlineCloseRuntime() : last_eval_ts(0.0), snap_close(0.0) {}
};

struct KlineCloseFetchResult {
  bool ok;
  double close;
  std::string note;
  std::string ref_bar;
  std::string time_key;

  KlineCloseFetchResult() : ok(false), close(0.0) {}
};

struct KlineCloseEvalContext {
  double now;
  KlineCloseRuntime *runtime;
  bool has_fetch;
  KlineCloseFetchResult fetch;
  bool has_gate;
  bool gate_ok;
  std::string gate_note;

  KlineCloseEvalContext()
      : now(0.0),
        runtime(NULL),
        has_fetch(false),
        has_gate(false),
        gate_ok(false) {}
};

EvalResult EvaluateKlineClose(const KlineCloseCondition &cond, const KlineCloseEvalContext &ctx);

}  // namespace engine
}  // namespace mmtrade
