#include "engine/registry.hpp"

namespace mmtrade {
namespace engine {

EvalResult EvaluateExitCondition(const ExitEvalBundle &bundle) {
  switch (bundle.kind) {
    case config::ConditionKind::kPrice:
      return EvaluateOrderbook(bundle.price, bundle.fields);
    case config::ConditionKind::kPnl:
      return EvaluatePnl(bundle.pnl, bundle.pnl_ctx);
    case config::ConditionKind::kKlineClose:
      return EvaluateKlineClose(bundle.kline_close, bundle.kline_close_ctx);
    case config::ConditionKind::kTrailingTakeProfit:
      return EvaluateTrailingTakeProfit(bundle.trailing_tp, bundle.trailing_tp_ctx);
    case config::ConditionKind::kStagedTakeProfit:
      return EvaluateStagedTakeProfit(bundle.staged_tp, bundle.staged_tp_ctx);
    case config::ConditionKind::kTrailingMa20Close:
      return EvaluateTrailingMa20Close(bundle.trailing_ma20_close,
                                       bundle.trailing_ma20_close_ctx);
    case config::ConditionKind::kTrailingMa20Realtime:
      return EvaluateTrailingMa20Realtime(bundle.trailing_ma20_rt,
                                          bundle.trailing_ma20_rt_ctx);
    case config::ConditionKind::kBeforeSessionClose:
      return EvaluateBeforeSessionClose(bundle.before_session_close,
                                        bundle.before_session_close_ctx);
    case config::ConditionKind::kPreEntryBarsLowStop:
      return EvaluatePreEntryBarsLowStop(bundle.pre_entry_low_stop,
                                         bundle.pre_entry_low_stop_ctx);
    default:
      break;
  }
  EvalResult out;
  out.handled = false;
  out.hit = false;
  out.reason = "not_registered";
  return out;
}

EvalResult EvaluateEntryCondition(const EntryEvalBundle &bundle) {
  switch (bundle.kind) {
    case config::ConditionKind::kPrice:
      return EvaluateOrderbook(bundle.price, bundle.fields);
    case config::ConditionKind::kVolume:
      return EvaluateVolume(bundle.volume, bundle.volume_ctx);
    case config::ConditionKind::kKlineClose:
      return EvaluateKlineClose(bundle.kline_close, bundle.kline_close_ctx);
    case config::ConditionKind::kKlineCandle:
      return EvaluateKlineCandle(bundle.kline_candle, bundle.kline_candle_ctx);
    case config::ConditionKind::kTrailingTakeProfit:
      return EvaluateTrailingTakeProfit(bundle.trailing_tp, bundle.trailing_tp_ctx);
    case config::ConditionKind::kDayOpenGain:
      return EvaluateDayOpenGain(bundle.day_open_gain, bundle.day_open_gain_ctx);
    case config::ConditionKind::kTrailingMa20Close:
      return EvaluateTrailingMa20Close(bundle.trailing_ma20_close,
                                       bundle.trailing_ma20_close_ctx);
    case config::ConditionKind::kTrailingMa20Realtime:
      return EvaluateTrailingMa20Realtime(bundle.trailing_ma20_rt,
                                          bundle.trailing_ma20_rt_ctx);
    default:
      break;
  }
  EvalResult out;
  out.handled = false;
  out.hit = false;
  out.reason = "not_registered";
  return out;
}

}  // namespace engine
}  // namespace mmtrade
