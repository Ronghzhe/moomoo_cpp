#pragma once

#include "engine/condition_types.hpp"
#include "engine/kline_candle_util.hpp"

#include <map>
#include <string>

namespace mmtrade {
namespace engine {

struct KlineCandleRuntime {
  double last_eval_ts;
  double snap_open;
  double snap_close;
  double snap_ref_px;
  double prev_close;
  std::string snap_ref_bar;
  std::string snap_time_key;
  std::string snap_note;
  std::map<std::string, bool> once_done;
  std::map<std::string, bool> once_ok;

  KlineCandleRuntime()
      : last_eval_ts(0.0),
        snap_open(0.0),
        snap_close(0.0),
        snap_ref_px(0.0),
        prev_close(0.0) {}
};

struct KlineCandleFetchResult {
  bool ok;
  double open;
  double close;
  std::string note;
  std::string ref_bar;
  std::string time_key;

  KlineCandleFetchResult() : ok(false), open(0.0), close(0.0) {}
};

struct KlineCandleEvalContext {
  double now;
  std::string rule_id;
  int cond_idx;
  KlineCandleRuntime *runtime;
  bool has_fetch;
  KlineCandleFetchResult fetch;
  bool has_gate;
  bool gate_ok;
  std::string gate_note;

  KlineCandleEvalContext()
      : now(0.0),
        cond_idx(-1),
        runtime(NULL),
        has_fetch(false),
        has_gate(false),
        gate_ok(false) {}
};

EvalResult EvaluateKlineCandle(const KlineCandleCondition &cond,
                               const KlineCandleEvalContext &ctx);

}  // namespace engine
}  // namespace mmtrade
