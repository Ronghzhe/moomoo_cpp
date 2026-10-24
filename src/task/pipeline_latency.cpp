#include "task/pipeline_latency.hpp"

#include "task/task_latency_recorder.hpp"

#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

namespace mmtrade {
namespace task {

namespace {

const uint32_t kTlsCap = 32;

enum SlotKind { kSlotNum = 1, kSlotStr = 2 };

struct TlsSlot {
  uint32_t epoch;
  uint8_t kind;
  const char *field;
  std::int64_t value;
  char text[24];
};

struct TlsBuf {
  TlsSlot slots[kTlsCap];
  std::atomic<uint32_t> w;
  std::atomic<uint32_t> r;
  TlsBuf *next;

  TlsBuf() : w(0), r(0), next(NULL) { std::memset(slots, 0, sizeof(slots)); }
  ~TlsBuf();
};

std::mutex g_reg_mu;
TlsBuf *g_bufs = NULL;
std::vector<std::pair<std::string, std::int64_t> > g_spill_nums;
std::vector<std::pair<std::string, std::string> > g_spill_strs;

struct Trace {
  std::atomic<bool> md_opend;
  std::atomic<bool> md_sdk;
  std::atomic<bool> md_proto;
  std::atomic<bool> md_callback;
  std::atomic<bool> md_wakeup;
  std::atomic<bool> entry_strategy;
  std::atomic<bool> entry_risk;
  std::atomic<bool> entry_sent;
  std::atomic<bool> entry_ack;
  std::atomic<bool> entry_terminal;
  std::atomic<bool> entry_terminal_kind;
  std::atomic<bool> exit_strategy;
  std::atomic<bool> exit_risk;
  std::atomic<bool> exit_sent;
  std::atomic<bool> exit_ack;
  std::atomic<bool> exit_terminal;
  std::atomic<bool> exit_terminal_kind;

  Trace()
      : md_opend(false),
        md_sdk(false),
        md_proto(false),
        md_callback(false),
        md_wakeup(false),
        entry_strategy(false),
        entry_risk(false),
        entry_sent(false),
        entry_ack(false),
        entry_terminal(false),
        entry_terminal_kind(false),
        exit_strategy(false),
        exit_risk(false),
        exit_sent(false),
        exit_ack(false),
        exit_terminal(false),
        exit_terminal_kind(false) {}

  void ClearFlags() {
    md_opend.store(false, std::memory_order_release);
    md_sdk.store(false, std::memory_order_release);
    md_proto.store(false, std::memory_order_release);
    md_callback.store(false, std::memory_order_release);
    md_wakeup.store(false, std::memory_order_release);
    entry_strategy.store(false, std::memory_order_release);
    entry_risk.store(false, std::memory_order_release);
    entry_sent.store(false, std::memory_order_release);
    entry_ack.store(false, std::memory_order_release);
    entry_terminal.store(false, std::memory_order_release);
    entry_terminal_kind.store(false, std::memory_order_release);
    exit_strategy.store(false, std::memory_order_release);
    exit_risk.store(false, std::memory_order_release);
    exit_sent.store(false, std::memory_order_release);
    exit_ack.store(false, std::memory_order_release);
    exit_terminal.store(false, std::memory_order_release);
    exit_terminal_kind.store(false, std::memory_order_release);
  }
};

std::atomic<uint32_t> g_epoch(1);
Trace g_trace;

thread_local TlsBuf g_tls;
thread_local bool g_tls_registered = false;

TlsBuf *Tls() {
  if (!g_tls_registered) {
    std::lock_guard<std::mutex> lk(g_reg_mu);
    if (!g_tls_registered) {
      g_tls.next = g_bufs;
      g_bufs = &g_tls;
      g_tls_registered = true;
    }
  }
  return &g_tls;
}

bool Claim(std::atomic<bool> *flag) {
  if (flag == NULL) {
    return false;
  }
  if (!TaskLatencyRecorder::Instance().Enabled()) {
    return false;
  }
  bool expected = false;
  return flag->compare_exchange_strong(expected, true, std::memory_order_acq_rel);
}

void PublishNum(const char *field, std::int64_t value) {
  if (field == NULL) {
    return;
  }
  TlsBuf *b = Tls();
  const uint32_t i = b->w.load(std::memory_order_relaxed);
  const uint32_t unread = i - b->r.load(std::memory_order_acquire);
  if (unread >= kTlsCap) {
    return;
  }
  TlsSlot &s = b->slots[i % kTlsCap];
  s.epoch = g_epoch.load(std::memory_order_relaxed);
  s.kind = kSlotNum;
  s.field = field;
  s.value = value;
  s.text[0] = '\0';
  b->w.store(i + 1, std::memory_order_release);
  TaskLatencyRecorder::Instance().WakeMerge();
}

void PublishStr(const char *field, const char *value) {
  if (field == NULL || value == NULL) {
    return;
  }
  TlsBuf *b = Tls();
  const uint32_t i = b->w.load(std::memory_order_relaxed);
  const uint32_t unread = i - b->r.load(std::memory_order_acquire);
  if (unread >= kTlsCap) {
    return;
  }
  TlsSlot &s = b->slots[i % kTlsCap];
  s.epoch = g_epoch.load(std::memory_order_relaxed);
  s.kind = kSlotStr;
  s.field = field;
  s.value = 0;
  std::strncpy(s.text, value, sizeof(s.text) - 1);
  s.text[sizeof(s.text) - 1] = '\0';
  b->w.store(i + 1, std::memory_order_release);
  TaskLatencyRecorder::Instance().WakeMerge();
}

void OnceTs(std::atomic<bool> *flag, const char *field) {
  if (!Claim(flag)) {
    return;
  }
  PublishNum(field, TaskLatencyRecorder::WallMsNow());
}

void OnceValue(std::atomic<bool> *flag, const char *field, std::int64_t value) {
  if (value <= 0 || !Claim(flag)) {
    return;
  }
  PublishNum(field, value);
}

void OnceString(std::atomic<bool> *flag, const char *field, const char *value) {
  if (value == NULL || !Claim(flag)) {
    return;
  }
  PublishStr(field, value);
}

void DrainOneBuf(TlsBuf *b, std::vector<std::pair<std::string, std::int64_t> > *nums,
                 std::vector<std::pair<std::string, std::string> > *strs) {
  if (b == NULL) {
    return;
  }
  const uint32_t epoch = g_epoch.load(std::memory_order_acquire);
  const uint32_t w = b->w.load(std::memory_order_acquire);
  uint32_t r = b->r.load(std::memory_order_relaxed);
  while (r != w) {
    const TlsSlot &s = b->slots[r % kTlsCap];
    ++r;
    if (s.field == NULL || s.epoch != epoch) {
      continue;
    }
    if (s.kind == kSlotStr) {
      if (strs != NULL) {
        strs->push_back(std::make_pair(std::string(s.field), std::string(s.text)));
      }
    } else if (nums != NULL) {
      nums->push_back(std::make_pair(std::string(s.field), s.value));
    }
  }
  b->r.store(r, std::memory_order_release);
}

TlsBuf::~TlsBuf() {
  std::lock_guard<std::mutex> lk(g_reg_mu);
  DrainOneBuf(this, &g_spill_nums, &g_spill_strs);
  TlsBuf **pp = &g_bufs;
  while (*pp != NULL) {
    if (*pp == this) {
      *pp = (*pp)->next;
      break;
    }
    pp = &(*pp)->next;
  }
}

void DrainLocked(std::vector<std::pair<std::string, std::int64_t> > *nums,
                 std::vector<std::pair<std::string, std::string> > *strs) {
  if (nums != NULL && !g_spill_nums.empty()) {
    nums->insert(nums->end(), g_spill_nums.begin(), g_spill_nums.end());
    g_spill_nums.clear();
  }
  if (strs != NULL && !g_spill_strs.empty()) {
    strs->insert(strs->end(), g_spill_strs.begin(), g_spill_strs.end());
    g_spill_strs.clear();
  }
  for (TlsBuf *b = g_bufs; b != NULL; b = b->next) {
    DrainOneBuf(b, nums, strs);
  }
}

}  // namespace

void PipelineLatency::OnMdOpenDRecvMs(std::int64_t exchange_recv_ms) {
  OnceValue(&g_trace.md_opend, "md_opend_recv_ms", exchange_recv_ms);
}

void PipelineLatency::OnMdSdkRecv() { OnceTs(&g_trace.md_sdk, "md_sdk_recv_ms"); }

void PipelineLatency::OnMdProtoDecoded() { OnceTs(&g_trace.md_proto, "md_proto_done_ms"); }

void PipelineLatency::OnMdCallbackNotified() { OnceTs(&g_trace.md_callback, "md_callback_ms"); }

void PipelineLatency::OnMdLoopWakeup() { OnceTs(&g_trace.md_wakeup, "md_loop_wakeup_ms"); }

void PipelineLatency::OnEntryStrategyDone() {
  OnceTs(&g_trace.entry_strategy, "entry_pipe_strategy_ms");
}

void PipelineLatency::OnEntryRiskPass() { OnceTs(&g_trace.entry_risk, "entry_pipe_risk_ms"); }

void PipelineLatency::OnEntryOrderSent() { OnceTs(&g_trace.entry_sent, "entry_pipe_order_sent_ms"); }

void PipelineLatency::OnEntryOrderAck() { OnceTs(&g_trace.entry_ack, "entry_pipe_order_ack_ms"); }

void PipelineLatency::OnEntryOrderTerminal(const char *kind) {
  OnceTs(&g_trace.entry_terminal, "entry_pipe_order_terminal_ms");
  OnceString(&g_trace.entry_terminal_kind, "entry_pipe_terminal_kind", kind);
}

void PipelineLatency::OnExitStrategyDone() { OnceTs(&g_trace.exit_strategy, "exit_pipe_strategy_ms"); }

void PipelineLatency::OnExitRiskPass() { OnceTs(&g_trace.exit_risk, "exit_pipe_risk_ms"); }

void PipelineLatency::OnExitOrderSent() { OnceTs(&g_trace.exit_sent, "exit_pipe_order_sent_ms"); }

void PipelineLatency::OnExitOrderAck() { OnceTs(&g_trace.exit_ack, "exit_pipe_order_ack_ms"); }

void PipelineLatency::OnExitOrderTerminal(const char *kind) {
  OnceTs(&g_trace.exit_terminal, "exit_pipe_order_terminal_ms");
  OnceString(&g_trace.exit_terminal_kind, "exit_pipe_terminal_kind", kind);
}

void PipelineLatency::Reset() {
  g_epoch.fetch_add(1, std::memory_order_acq_rel);
  g_trace.ClearFlags();
}

void PipelineLatency::DrainPending(std::vector<std::pair<std::string, std::int64_t> > *nums,
                                   std::vector<std::pair<std::string, std::string> > *strs) {
  std::lock_guard<std::mutex> lk(g_reg_mu);
  DrainLocked(nums, strs);
}

bool PipelineLatency::HasPending() {
  std::lock_guard<std::mutex> lk(g_reg_mu);
  if (!g_spill_nums.empty() || !g_spill_strs.empty()) {
    return true;
  }
  const uint32_t epoch = g_epoch.load(std::memory_order_acquire);
  for (TlsBuf *b = g_bufs; b != NULL; b = b->next) {
    const uint32_t w = b->w.load(std::memory_order_acquire);
    uint32_t r = b->r.load(std::memory_order_relaxed);
    while (r != w) {
      const TlsSlot &s = b->slots[r % kTlsCap];
      ++r;
      if (s.field != NULL && s.epoch == epoch) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace task
}  // namespace mmtrade
