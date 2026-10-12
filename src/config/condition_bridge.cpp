#include "config/condition_bridge.hpp"

namespace mmtrade {
namespace config {

void FillExitEvalBundle(const Condition &cond, engine::ExitEvalBundle *out) {
  if (out == NULL) {
    return;
  }
  out->kind = cond.kind;
  out->price = cond.price;
  out->pnl = cond.pnl;
  out->kline_close = cond.kline_close;
  out->trailing_tp = cond.trailing_tp;
  out->staged_tp = cond.staged_tp;
  out->trailing_ma20_close = cond.trailing_ma20_close;
  out->trailing_ma20_rt = cond.trailing_ma20_rt;
  out->before_session_close = cond.before_session_close;
  out->pre_entry_low_stop = cond.pre_entry_low_stop;
}

void FillEntryEvalBundle(const Condition &cond, engine::EntryEvalBundle *out) {
  if (out == NULL) {
    return;
  }
  out->kind = cond.kind;
  out->price = cond.price;
  out->volume = cond.volume;
  out->kline_close = cond.kline_close;
  out->kline_candle = cond.kline_candle;
  out->trailing_tp = cond.trailing_tp;
  out->day_open_gain = cond.day_open_gain;
  out->trailing_ma20_close = cond.trailing_ma20_close;
  out->trailing_ma20_rt = cond.trailing_ma20_rt;
}

}  // namespace config
}  // namespace mmtrade
