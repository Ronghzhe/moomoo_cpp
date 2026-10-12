#include "common/rate_limiter.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

namespace mmtrade {
namespace common {

namespace {

double SteadyNowSec() {
  using clock = std::chrono::steady_clock;
  return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

}  // namespace

TokenBucket::TokenBucket(double rate_per_sec, double burst)
    : rate_per_sec_(rate_per_sec), burst_(burst), tokens_(burst), last_ts_(SteadyNowSec()) {}

bool TokenBucket::Allow(double cost) {
  const double now = SteadyNowSec();
  const double elapsed = std::max(0.0, now - last_ts_);
  last_ts_ = now;
  tokens_ = std::min(burst_, tokens_ + elapsed * rate_per_sec_);
  if (tokens_ >= cost) {
    tokens_ -= cost;
    return true;
  }
  return false;
}

AdaptiveRate::AdaptiveRate(double base_rate_per_sec, double min_rate_per_sec, double decay_factor,
                           double recover_step)
    : base_(base_rate_per_sec),
      min_rate_(min_rate_per_sec),
      decay_factor_(decay_factor),
      recover_step_(recover_step),
      current_(base_rate_per_sec) {}

double AdaptiveRate::OnFrequencyError() {
  current_ = std::max(min_rate_, current_ * decay_factor_);
  return current_;
}

double AdaptiveRate::OnSuccessTick() {
  if (current_ < base_) {
    current_ = std::min(base_, current_ + recover_step_);
  }
  return current_;
}

MoomooPlaceOrderLimiter::MoomooPlaceOrderLimiter(MoomooRateConfig cfg) : cfg_(cfg) {}

int MoomooPlaceOrderLimiter::EffectiveMax() const {
  return std::max(1, cfg_.max_requests - std::max(0, cfg_.reserve_slots));
}

std::string MoomooPlaceOrderLimiter::NormalizeAccKey(const std::string &acc_key) {
  if (acc_key.empty()) {
    return "0";
  }
  return acc_key;
}

double MoomooPlaceOrderLimiter::NowMono() { return SteadyNowSec(); }

void MoomooPlaceOrderLimiter::Prune(const std::string &acc_key, double now) const {
  const std::string k = NormalizeAccKey(acc_key);
  TimestampDeque &dq = timestamps_[k];
  const double cutoff = now - cfg_.window_sec;
  while (!dq.empty() && dq.front() < cutoff) {
    dq.pop_front();
  }
}

double MoomooPlaceOrderLimiter::SecondsUntilAllowedUnlocked(const std::string &acc_key,
                                                              double now) const {
  const std::string k = NormalizeAccKey(acc_key);

  const auto cooldown_it = cooldown_until_.find(k);
  if (cooldown_it != cooldown_until_.end() && now < cooldown_it->second) {
    return cooldown_it->second - now;
  }

  const auto last_it = last_mono_.find(k);
  if (last_it != last_mono_.end()) {
    const double gap = cfg_.min_interval_sec - (now - last_it->second);
    if (gap > 0.0) {
      return gap;
    }
  }

  Prune(k, now);
  const TimestampDeque &dq = timestamps_.at(k);
  if (static_cast<int>(dq.size()) >= EffectiveMax()) {
    const double oldest = dq.front();
    return std::max(0.0, oldest + cfg_.window_sec - now);
  }
  return 0.0;
}

double MoomooPlaceOrderLimiter::SecondsUntilAllowed(const std::string &acc_key, double now) const {
  std::lock_guard<std::mutex> lock(mu_);
  return SecondsUntilAllowedUnlocked(acc_key, now);
}

void MoomooPlaceOrderLimiter::RecordPlaceUnlocked(const std::string &acc_key, double now) {
  const std::string k = NormalizeAccKey(acc_key);
  Prune(k, now);
  timestamps_[k].push_back(now);
  last_mono_[k] = now;
}

void MoomooPlaceOrderLimiter::RecordPlace(const std::string &acc_key, double now) {
  std::lock_guard<std::mutex> lock(mu_);
  RecordPlaceUnlocked(acc_key, now);
}

double MoomooPlaceOrderLimiter::WaitUntilAllowed(const std::string &acc_key, double max_wait_sec) {
  const std::string k = NormalizeAccKey(acc_key);
  double waited = 0.0;
  while (true) {
    double delay = 0.0;
    {
      std::lock_guard<std::mutex> lock(mu_);
      delay = SecondsUntilAllowedUnlocked(k, NowMono());
      if (delay <= 0.0) {
        RecordPlaceUnlocked(k, NowMono());
        return waited;
      }
    }
    if (max_wait_sec >= 0.0 && waited >= max_wait_sec) {
      return waited;
    }
    double sleep_for = std::min(std::max(delay, 0.0), 0.05);
    if (max_wait_sec >= 0.0) {
      sleep_for = std::min(sleep_for, std::max(0.0, max_wait_sec - waited));
    }
    if (sleep_for <= 0.0) {
      return waited;
    }
    std::this_thread::sleep_for(
        std::chrono::milliseconds(static_cast<int>(sleep_for * 1000.0)));
    waited += sleep_for;
  }
}

void MoomooPlaceOrderLimiter::OnFrequencyError(const std::string &acc_key, double backoff_sec,
                                               double now) {
  const std::string k = NormalizeAccKey(acc_key);
  const double t = (now >= 0.0) ? now : NowMono();
  const double until = t + std::max(0.05, backoff_sec);
  std::lock_guard<std::mutex> lock(mu_);
  const auto it = cooldown_until_.find(k);
  const double prev = (it != cooldown_until_.end()) ? it->second : 0.0;
  cooldown_until_[k] = std::max(prev, until);
}

PlaceLimiterSnapshot MoomooPlaceOrderLimiter::Snapshot(const std::string &acc_key, double now) const {
  const std::string k = NormalizeAccKey(acc_key);
  std::lock_guard<std::mutex> lock(mu_);
  Prune(k, now);
  PlaceLimiterSnapshot out;
  out.used_in_window = static_cast<int>(timestamps_[k].size());
  out.max_in_window = EffectiveMax();
  out.window_sec = cfg_.window_sec;
  out.min_interval_sec = cfg_.min_interval_sec;
  out.wait_sec = SecondsUntilAllowedUnlocked(k, now);
  return out;
}

}  // namespace common
}  // namespace mmtrade
