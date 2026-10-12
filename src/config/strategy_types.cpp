#include "config/strategy_types.hpp"

namespace mmtrade {
namespace config {

const char *ConditionPythonName(ConditionKind kind) {
  switch (kind) {
    case ConditionKind::kPrice:
      return "PriceCondition";
    case ConditionKind::kDayOpenGain:
      return "DayOpenGainWhen";
    case ConditionKind::kVolume:
      return "VolumeEntryWhen";
    case ConditionKind::kKlineClose:
      return "KlineCloseCondition";
    case ConditionKind::kKlineCandle:
      return "KlineCandleCondition";
    case ConditionKind::kTrailingTakeProfit:
      return "TrailingTakeProfitCondition";
    case ConditionKind::kTrailingMa20Close:
      return "TrailingMA20CloseCondition";
    case ConditionKind::kTrailingMa20Realtime:
      return "TrailingMA20RealtimeCondition";
    case ConditionKind::kPnl:
      return "PnlCondition";
    case ConditionKind::kStagedTakeProfit:
      return "StagedTakeProfitCondition";
    case ConditionKind::kBeforeSessionClose:
      return "BeforeSessionCloseCondition";
    case ConditionKind::kPreEntryBarsLowStop:
      return "PreEntryBarsLowStopCondition";
  }
  return "UnknownCondition";
}

}  // namespace config
}  // namespace mmtrade
