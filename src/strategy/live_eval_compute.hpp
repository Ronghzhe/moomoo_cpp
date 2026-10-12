#pragma once

#include "config/strategy_types.hpp"
#include "engine/condition_types.hpp"
#include "engine/eval/kline_candle_eval.hpp"
#include "engine/eval/orderbook_eval.hpp"
#include "engine/eval/trailing_ma20_close_eval.hpp"
#include "engine/eval/trailing_ma20_realtime_eval.hpp"
#include "engine/eval/volume_eval.hpp"
#include "strategy/tick_context.hpp"

#include <memory>
#include <string>

namespace mmtrade {
namespace strategy {

class LiveEvalCompute {
 public:
  LiveEvalCompute(const std::string &host, int port, const std::string &code_full);
  ~LiveEvalCompute();

  bool Connect();
  void Close();
  void ResetTick();

  bool AfterOpenGate(const StrategyTickContext &tick, int after_open_minutes) const;

  void FillKlineClose(const engine::KlineCloseCondition &cond, const StrategyTickContext &tick,
                      engine::KlineCloseEvalContext *ctx);

  void FillKlineCandle(const engine::KlineCandleCondition &cond, const StrategyTickContext &tick,
                       engine::KlineCandleEvalContext *ctx);

  void FillVolume(const engine::VolumeEntryWhen &cond, const StrategyTickContext &tick,
                  engine::VolumeEvalContext *ctx);

  void FillTrailingMa20Close(const engine::TrailingMa20CloseCondition &cond,
                             const StrategyTickContext &tick,
                             engine::TrailingMa20CloseEvalContext *ctx);

  void FillTrailingMa20Rt(const engine::TrailingMa20RealtimeCondition &cond,
                          const StrategyTickContext &tick,
                          engine::TrailingMa20RtEvalContext *ctx);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace strategy
}  // namespace mmtrade
