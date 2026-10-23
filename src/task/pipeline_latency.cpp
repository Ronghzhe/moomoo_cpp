#include "task/pipeline_latency.hpp"

#include "task/task_latency_recorder.hpp"

#include <mutex>

namespace mmtrade {
namespace task {

namespace {

std::mutex g_mu;
bool md_opend = false;
bool md_sdk = false;
bool md_proto = false;
bool md_callback = false;
bool md_wakeup = false;
bool entry_strategy = false;
bool entry_risk = false;
bool entry_sent = false;
bool entry_ack = false;
bool entry_terminal = false;
bool entry_terminal_kind_set = false;
bool exit_strategy = false;
bool exit_risk = false;
bool exit_sent = false;
bool exit_ack = false;
bool exit_terminal = false;
bool exit_terminal_kind_set = false;

void OnceTs(bool *flag, const char *field) {
  if (flag == NULL || field == NULL || *flag) {
    return;
  }
  TaskLatencyRecorder &rec = TaskLatencyRecorder::Instance();
  if (!rec.Enabled()) {
    return;
  }
  *flag = true;
  rec.RecordTimestampMs(field);
}

void OnceValue(bool *flag, const char *field, std::int64_t value) {
  if (flag == NULL || field == NULL || *flag || value <= 0) {
    return;
  }
  TaskLatencyRecorder &rec = TaskLatencyRecorder::Instance();
  if (!rec.Enabled()) {
    return;
  }
  *flag = true;
  rec.RecordValue(field, value);
}

void OnceString(bool *flag, const char *field, const char *value) {
  if (flag == NULL || field == NULL || value == NULL || *flag) {
    return;
  }
  TaskLatencyRecorder &rec = TaskLatencyRecorder::Instance();
  if (!rec.Enabled()) {
    return;
  }
  *flag = true;
  rec.RecordString(field, value);
}

}  // namespace

void PipelineLatency::OnMdOpenDRecvMs(std::int64_t exchange_recv_ms) {
  std::lock_guard<std::mutex> lk(g_mu);
  OnceValue(&md_opend, "md_opend_recv_ms", exchange_recv_ms);
}

void PipelineLatency::OnMdSdkRecv() {
  std::lock_guard<std::mutex> lk(g_mu);
  OnceTs(&md_sdk, "md_sdk_recv_ms");
}

void PipelineLatency::OnMdProtoDecoded() {
  std::lock_guard<std::mutex> lk(g_mu);
  OnceTs(&md_proto, "md_proto_done_ms");
}

void PipelineLatency::OnMdCallbackNotified() {
  std::lock_guard<std::mutex> lk(g_mu);
  OnceTs(&md_callback, "md_callback_ms");
}

void PipelineLatency::OnMdLoopWakeup() {
  std::lock_guard<std::mutex> lk(g_mu);
  OnceTs(&md_wakeup, "md_loop_wakeup_ms");
}

void PipelineLatency::OnEntryStrategyDone() {
  std::lock_guard<std::mutex> lk(g_mu);
  OnceTs(&entry_strategy, "entry_pipe_strategy_ms");
}

void PipelineLatency::OnEntryRiskPass() {
  std::lock_guard<std::mutex> lk(g_mu);
  OnceTs(&entry_risk, "entry_pipe_risk_ms");
}

void PipelineLatency::OnEntryOrderSent() {
  std::lock_guard<std::mutex> lk(g_mu);
  OnceTs(&entry_sent, "entry_pipe_order_sent_ms");
}

void PipelineLatency::OnEntryOrderAck() {
  std::lock_guard<std::mutex> lk(g_mu);
  OnceTs(&entry_ack, "entry_pipe_order_ack_ms");
}

void PipelineLatency::OnEntryOrderTerminal(const char *kind) {
  std::lock_guard<std::mutex> lk(g_mu);
  OnceTs(&entry_terminal, "entry_pipe_order_terminal_ms");
  OnceString(&entry_terminal_kind_set, "entry_pipe_terminal_kind", kind);
}

void PipelineLatency::OnExitStrategyDone() {
  std::lock_guard<std::mutex> lk(g_mu);
  OnceTs(&exit_strategy, "exit_pipe_strategy_ms");
}

void PipelineLatency::OnExitRiskPass() {
  std::lock_guard<std::mutex> lk(g_mu);
  OnceTs(&exit_risk, "exit_pipe_risk_ms");
}

void PipelineLatency::OnExitOrderSent() {
  std::lock_guard<std::mutex> lk(g_mu);
  OnceTs(&exit_sent, "exit_pipe_order_sent_ms");
}

void PipelineLatency::OnExitOrderAck() {
  std::lock_guard<std::mutex> lk(g_mu);
  OnceTs(&exit_ack, "exit_pipe_order_ack_ms");
}

void PipelineLatency::OnExitOrderTerminal(const char *kind) {
  std::lock_guard<std::mutex> lk(g_mu);
  OnceTs(&exit_terminal, "exit_pipe_order_terminal_ms");
  OnceString(&exit_terminal_kind_set, "exit_pipe_terminal_kind", kind);
}

void PipelineLatency::ResetForTests() {
  std::lock_guard<std::mutex> lk(g_mu);
  md_opend = md_sdk = md_proto = md_callback = md_wakeup = false;
  entry_strategy = entry_risk = entry_sent = entry_ack = entry_terminal = false;
  entry_terminal_kind_set = false;
  exit_strategy = exit_risk = exit_sent = exit_ack = exit_terminal = false;
  exit_terminal_kind_set = false;
}

}  // namespace task
}  // namespace mmtrade
