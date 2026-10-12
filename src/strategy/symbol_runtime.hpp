#pragma once

#include "engine/eval/before_session_close_eval.hpp"
#include "engine/eval/day_open_gain_eval.hpp"
#include "engine/eval/kline_candle_eval.hpp"
#include "engine/eval/orderbook_eval.hpp"
#include "engine/eval/pre_entry_bars_low_stop_eval.hpp"
#include "engine/eval/trailing_ma20_close_eval.hpp"
#include "engine/eval/trailing_ma20_realtime_eval.hpp"
#include "engine/eval/trailing_take_profit_eval.hpp"
#include "engine/eval/volume_eval.hpp"
#include "engine/staged_take_profit_util.hpp"

namespace mmtrade {
namespace strategy {

struct SymbolEvalRuntime {
  engine::VolumeRuntime volume;
  engine::KlineCloseRuntime kline_close;
  engine::KlineCandleRuntime kline_candle;
  engine::TrailingTpRuntime trailing_tp;
  engine::DayOpenGainRuntime day_open_gain;
  engine::TrailingMa20CloseRuntime trailing_ma20_close;
  engine::TrailingMa20RtRuntime trailing_ma20_rt;
  engine::StagedTpRuntime staged_tp;
  engine::PreEntryLowStopRuntime pre_entry_low_stop;

  SymbolEvalRuntime() {}
};

}  // namespace strategy
}  // namespace mmtrade
