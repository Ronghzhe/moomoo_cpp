#pragma once

// End-to-end pipeline timestamps (market data → strategy → risk → order → ack → terminal).
// Each stage is recorded at most once per strategy leg when TaskLatencyRecorder is enabled.

#include <cstdint>

namespace mmtrade {
namespace task {

class PipelineLatency {
 public:
  // 1–4: Quote push path (first orderbook push in this process).
  static void OnMdOpenDRecvMs(std::int64_t exchange_recv_ms);
  static void OnMdSdkRecv();
  static void OnMdProtoDecoded();
  static void OnMdCallbackNotified();

  // After WaitBookPush returns (strategy thread woke for book update).
  static void OnMdLoopWakeup();

  // 5–9: Entry leg (first entry hit → first terminal fill/cancel/fail).
  static void OnEntryStrategyDone();
  static void OnEntryRiskPass();
  static void OnEntryOrderSent();
  static void OnEntryOrderAck();
  static void OnEntryOrderTerminal(const char *kind);

  // 5–9: Exit leg.
  static void OnExitStrategyDone();
  static void OnExitRiskPass();
  static void OnExitOrderSent();
  static void OnExitOrderAck();
  static void OnExitOrderTerminal(const char *kind);

  static void ResetForTests();
};

}  // namespace task
}  // namespace mmtrade
