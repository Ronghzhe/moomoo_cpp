#include "strategy/live_eval_compute.hpp"

#include "opend/kline_connection.hpp"
#include "quote/kline_parse.hpp"
#include "strategy/exchange_clock.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace mmtrade {
namespace strategy {

namespace {

int SessionOpenMinute(const std::string &code_full) {
  if (code_full.find("HK.") == 0) {
    return 9 * 60 + 30;
  }
  return 9 * 60 + 30;
}

double AverageVolume(const std::vector<opend::KlineBar> &bars, int ma_period, bool include_last) {
  if (bars.empty() || ma_period <= 0) {
    return 0.0;
  }
  const int n = static_cast<int>(bars.size());
  int start = include_last ? std::max(0, n - ma_period) : std::max(0, n - ma_period - 1);
  int end = include_last ? n : std::max(start, n - 1);
  if (end <= start) {
    return 0.0;
  }
  double sum = 0.0;
  int cnt = 0;
  for (int i = start; i < end; ++i) {
    sum += bars[static_cast<size_t>(i)].volume;
    ++cnt;
  }
  return cnt > 0 ? sum / static_cast<double>(cnt) : 0.0;
}

double SimpleMaClose(const std::vector<opend::KlineBar> &bars, int period, bool include_last) {
  if (bars.empty() || period <= 0) {
    return 0.0;
  }
  const int n = static_cast<int>(bars.size());
  int start = include_last ? std::max(0, n - period) : std::max(0, n - period - 1);
  int end = include_last ? n : std::max(start, n - 1);
  if (end <= start) {
    return 0.0;
  }
  double sum = 0.0;
  int cnt = 0;
  for (int i = start; i < end; ++i) {
    sum += bars[static_cast<size_t>(i)].close;
    ++cnt;
  }
  return cnt > 0 ? sum / static_cast<double>(cnt) : 0.0;
}

}  // namespace

struct BarCache {
  bool ok;
  std::vector<opend::KlineBar> bars;

  BarCache() : ok(false) {}
};

bool SelectClosedBar(const BarCache &cache, const opend::KlineBar **out_bar,
                     const opend::KlineBar **out_prev) {
  if (!cache.ok || cache.bars.size() < 2) {
    return false;
  }
  *out_bar = &cache.bars[cache.bars.size() - 2];
  if (cache.bars.size() >= 3) {
    *out_prev = &cache.bars[cache.bars.size() - 3];
  } else {
    *out_prev = NULL;
  }
  return true;
}

struct LiveEvalCompute::Impl {
  std::string host;
  int port;
  std::string code_full;
  bool conn_connected;
  std::unique_ptr<opend::KlineConnection> conn;
  std::map<std::string, BarCache> bar_cache;

  Impl() : port(0), conn_connected(false) {}

  const BarCache &FetchBars(const std::string &kl_type, int count) {
    const int req = count > 0 ? count : 30;
    const std::string key = kl_type + ":" + std::to_string(req);
    std::map<std::string, BarCache>::iterator it = bar_cache.find(key);
    if (it != bar_cache.end()) {
      return it->second;
    }
    if (conn.get() == NULL) {
      conn.reset(new opend::KlineConnection());
    }
    if (!conn_connected && !conn->Connect(host, port)) {
      BarCache empty;
      bar_cache[key] = empty;
      return bar_cache[key];
    }
    conn_connected = true;
    BarCache cache;
    const quote::KlTypeKey kl = quote::ParseKlTypeKey(kl_type);
    const opend::KlineFetchResult fetched = conn->Fetch(code_full, kl, req, true, false);
    cache.ok = fetched.ok;
    cache.bars = fetched.bars;
    bar_cache[key] = cache;
    return bar_cache[key];
  }
};

LiveEvalCompute::LiveEvalCompute(const std::string &host, int port, const std::string &code_full)
    : impl_(new Impl()) {
  impl_->host = host;
  impl_->port = port;
  impl_->code_full = code_full;
}

LiveEvalCompute::~LiveEvalCompute() { Close(); }

bool LiveEvalCompute::Connect() {
  return impl_.get() != NULL;
}

void LiveEvalCompute::Close() {
  if (impl_.get() != NULL && impl_->conn.get() != NULL) {
    impl_->conn->Disconnect();
  }
  if (impl_.get() != NULL) {
    impl_->bar_cache.clear();
  }
}

void LiveEvalCompute::ResetTick() {
  if (impl_.get() != NULL) {
    impl_->bar_cache.clear();
  }
}

bool LiveEvalCompute::AfterOpenGate(const StrategyTickContext &tick,
                                    int after_open_minutes) const {
  if (!tick.has_exchange_local_now) {
    return after_open_minutes <= 0;
  }
  const engine::LocalDateTime &local = tick.exchange_local_now;
  const int now_min = local.hour * 60 + local.minute;
  return (now_min - SessionOpenMinute(impl_->code_full)) >= after_open_minutes;
}

void LiveEvalCompute::FillKlineClose(const engine::KlineCloseCondition &cond,
                                     const StrategyTickContext &tick,
                                     engine::KlineCloseEvalContext *ctx) {
  if (ctx == NULL || impl_.get() == NULL) {
    return;
  }
  ctx->has_gate = true;
  ctx->gate_ok = AfterOpenGate(tick, cond.after_open_minutes);
  ctx->gate_note = ctx->gate_ok ? "after_open_ok" : "before_after_open";
  const BarCache &cache = impl_->FetchBars(cond.kl_type, 30);
  ctx->has_fetch = true;
  const opend::KlineBar *bar = NULL;
  const opend::KlineBar *prev = NULL;
  if (SelectClosedBar(cache, &bar, &prev)) {
    ctx->fetch.ok = true;
    ctx->fetch.close = bar->close;
    ctx->fetch.ref_bar = bar->time_key;
    ctx->fetch.time_key = bar->time_key;
    ctx->fetch.note = "closed_bar";
  } else {
    ctx->fetch.ok = false;
    ctx->fetch.note = "no_bars";
  }
}

void LiveEvalCompute::FillKlineCandle(const engine::KlineCandleCondition &cond,
                                      const StrategyTickContext &tick,
                                      engine::KlineCandleEvalContext *ctx) {
  if (ctx == NULL || impl_.get() == NULL) {
    return;
  }
  ctx->has_gate = true;
  ctx->gate_ok = AfterOpenGate(tick, cond.after_open_minutes);
  const BarCache &cache = impl_->FetchBars(cond.kl_type, 30);
  ctx->has_fetch = true;
  const opend::KlineBar *bar = NULL;
  const opend::KlineBar *prev = NULL;
  if (SelectClosedBar(cache, &bar, &prev)) {
    ctx->fetch.ok = true;
    ctx->fetch.open = bar->open;
    ctx->fetch.close = bar->close;
    ctx->fetch.ref_bar = bar->time_key;
    ctx->fetch.time_key = bar->time_key;
    ctx->fetch.note = "closed_bar";
    if (prev != NULL && ctx->runtime != NULL) {
      ctx->runtime->prev_close = prev->close;
    }
  } else {
    ctx->fetch.ok = false;
  }
}

void LiveEvalCompute::FillVolume(const engine::VolumeEntryWhen &cond,
                                 const StrategyTickContext &tick, engine::VolumeEvalContext *ctx) {
  if (ctx == NULL || impl_.get() == NULL) {
    return;
  }
  const int req = std::max(cond.ma_period + 5, 30);
  const BarCache &cache = impl_->FetchBars(cond.kl_type, req);
  ctx->has_compute = true;
  if (!cache.ok || cache.bars.size() < 2) {
    ctx->compute.hit = false;
    ctx->compute.note = "no_bars";
    return;
  }
  const opend::KlineBar &last_closed = cache.bars[cache.bars.size() - 2];
  const double ma = AverageVolume(cache.bars, cond.ma_period, cond.ma_include_last);
  const double thresh = ma * cond.volume_multiple;
  ctx->compute.ma = ma;
  ctx->compute.vol = last_closed.volume;
  ctx->compute.thresh = thresh;
  ctx->compute.ref_bar = last_closed.time_key;
  ctx->compute.hit = last_closed.volume + 1e-9 >= thresh && thresh > 0.0;
  ctx->compute.note = "volume_spike";
}

void LiveEvalCompute::FillTrailingMa20Close(const engine::TrailingMa20CloseCondition &cond,
                                            const StrategyTickContext &tick,
                                            engine::TrailingMa20CloseEvalContext *ctx) {
  if (ctx == NULL || impl_.get() == NULL) {
    return;
  }
  const int period = cond.ma_period > 0 ? cond.ma_period : 20;
  const BarCache &cache = impl_->FetchBars(cond.kl_type, period + 5);
  ctx->has_compute = true;
  const opend::KlineBar *bar = NULL;
  const opend::KlineBar *prev = NULL;
  if (SelectClosedBar(cache, &bar, &prev)) {
    ctx->compute.ok = true;
    ctx->compute.cur_close = bar->close;
    ctx->compute.ma_ref = SimpleMaClose(cache.bars, period, false);
    ctx->compute.ref_bar = bar->time_key;
    ctx->compute.bar_time_key = bar->time_key;
    ctx->compute.note = "ma_close";
  } else {
    ctx->compute.ok = false;
  }
}

void LiveEvalCompute::FillTrailingMa20Rt(const engine::TrailingMa20RealtimeCondition &cond,
                                         const StrategyTickContext &tick,
                                         engine::TrailingMa20RtEvalContext *ctx) {
  if (ctx == NULL || impl_.get() == NULL) {
    return;
  }
  const int period = cond.ma_period > 0 ? cond.ma_period : 20;
  const BarCache &cache = impl_->FetchBars(cond.kl_type, period + 5);
  ctx->has_compute = true;
  if (!cache.ok || cache.bars.empty()) {
    ctx->compute.ok = false;
    return;
  }
  ctx->compute.ok = true;
  ctx->compute.ma_ref = SimpleMaClose(cache.bars, period, true);
  ctx->compute.ref_bar = cache.bars.back().time_key;
  ctx->compute.bar_time_key = cache.bars.back().time_key;
  ctx->compute.note = "ma_rt";
}

}  // namespace strategy
}  // namespace mmtrade
