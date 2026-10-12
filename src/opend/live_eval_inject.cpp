#include "strategy/live_eval_inject.hpp"
#include "strategy/live_eval_compute.hpp"

#include "strategy/live_eval_compute.hpp"

namespace mmtrade {
namespace strategy {

void InjectLiveEntryEval(const config::Condition &cond, LiveEvalCompute *live,
                         const StrategyTickContext &tick, engine::EntryEvalBundle *bundle) {
  if (live == NULL || bundle == NULL) {
    return;
  }
  switch (cond.kind) {
    case config::ConditionKind::kVolume:
      live->FillVolume(cond.volume, tick, &bundle->volume_ctx);
      break;
    case config::ConditionKind::kKlineClose:
      live->FillKlineClose(cond.kline_close, tick, &bundle->kline_close_ctx);
      break;
    case config::ConditionKind::kKlineCandle:
      live->FillKlineCandle(cond.kline_candle, tick, &bundle->kline_candle_ctx);
      break;
    case config::ConditionKind::kTrailingMa20Close:
      live->FillTrailingMa20Close(cond.trailing_ma20_close, tick, &bundle->trailing_ma20_close_ctx);
      break;
    case config::ConditionKind::kTrailingMa20Realtime:
      live->FillTrailingMa20Rt(cond.trailing_ma20_rt, tick, &bundle->trailing_ma20_rt_ctx);
      break;
    default:
      break;
  }
}

void InjectLiveExitEval(const config::Condition &cond, LiveEvalCompute *live,
                        const StrategyTickContext &tick, engine::ExitEvalBundle *bundle) {
  if (live == NULL || bundle == NULL) {
    return;
  }
  switch (cond.kind) {
    case config::ConditionKind::kTrailingMa20Close:
      live->FillTrailingMa20Close(cond.trailing_ma20_close, tick, &bundle->trailing_ma20_close_ctx);
      break;
    case config::ConditionKind::kTrailingMa20Realtime:
      live->FillTrailingMa20Rt(cond.trailing_ma20_rt, tick, &bundle->trailing_ma20_rt_ctx);
      break;
    default:
      break;
  }
}

}  // namespace strategy
}  // namespace mmtrade
