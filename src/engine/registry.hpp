#pragma once

#include "config/strategy_types.hpp"
#include "engine/condition_types.hpp"
#include "engine/eval/before_session_close_eval.hpp"
#include "engine/eval/day_open_gain_eval.hpp"
#include "engine/eval/kline_candle_eval.hpp"
#include "engine/eval/kline_candle_eval.hpp"
#include "engine/eval/orderbook_eval.hpp"
#include "engine/eval/pre_entry_bars_low_stop_eval.hpp"
#include "engine/eval/trailing_ma20_close_eval.hpp"
#include "engine/eval/trailing_ma20_realtime_eval.hpp"
#include "engine/eval/trailing_take_profit_eval.hpp"
#include "engine/eval/volume_eval.hpp"
#include "engine/staged_take_profit_util.hpp"

namespace mmtrade {
namespace engine {

struct ExitEvalBundle {
  config::ConditionKind kind;
  PriceCondition price;
  PnlCondition pnl;
  KlineCloseCondition kline_close;
  TrailingTakeProfitCondition trailing_tp;
  StagedTakeProfitCondition staged_tp;
  TrailingMa20CloseCondition trailing_ma20_close;
  TrailingMa20RealtimeCondition trailing_ma20_rt;
  BeforeSessionCloseCondition before_session_close;
  PreEntryBarsLowStopCondition pre_entry_low_stop;
  FieldMap fields;
  PnlEvalContext pnl_ctx;
  KlineCloseEvalContext kline_close_ctx;
  TrailingTpEvalContext trailing_tp_ctx;
  StagedTpEvalContext staged_tp_ctx;
  TrailingMa20CloseEvalContext trailing_ma20_close_ctx;
  TrailingMa20RtEvalContext trailing_ma20_rt_ctx;
  BeforeSessionCloseEvalContext before_session_close_ctx;
  PreEntryLowStopEvalContext pre_entry_low_stop_ctx;

  ExitEvalBundle() : kind(config::ConditionKind::kPrice) {}
};

struct EntryEvalBundle {
  config::ConditionKind kind;
  PriceCondition price;
  VolumeEntryWhen volume;
  KlineCloseCondition kline_close;
  KlineCandleCondition kline_candle;
  TrailingTakeProfitCondition trailing_tp;
  DayOpenGainCondition day_open_gain;
  TrailingMa20CloseCondition trailing_ma20_close;
  TrailingMa20RealtimeCondition trailing_ma20_rt;
  FieldMap fields;
  VolumeEvalContext volume_ctx;
  KlineCloseEvalContext kline_close_ctx;
  KlineCandleEvalContext kline_candle_ctx;
  TrailingTpEvalContext trailing_tp_ctx;
  DayOpenGainEvalContext day_open_gain_ctx;
  TrailingMa20CloseEvalContext trailing_ma20_close_ctx;
  TrailingMa20RtEvalContext trailing_ma20_rt_ctx;

  EntryEvalBundle() : kind(config::ConditionKind::kPrice) {}
};

EvalResult EvaluateExitCondition(const ExitEvalBundle &bundle);
EvalResult EvaluateEntryCondition(const EntryEvalBundle &bundle);

}  // namespace engine
}  // namespace mmtrade
