#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

namespace mmtrade {
namespace common {

struct MoomooRateConfig {
  double window_sec = 30.0;
  int max_requests = 15;
  double min_interval_sec = 0.02;
  int reserve_slots = 1;
};

class TokenBucket {
 public:
  TokenBucket(double rate_per_sec, double burst);

  bool Allow(double cost = 1.0);

 private:
  double rate_per_sec_;
  double burst_;
  double tokens_;
  double last_ts_;
};

class AdaptiveRate {
 public:
  AdaptiveRate(double base_rate_per_sec, double min_rate_per_sec, double decay_factor = 0.75,
               double recover_step = 0.1);

  double current() const { return current_; }

  double OnFrequencyError();
  double OnSuccessTick();

 private:
  double base_;
  double min_rate_;
  double decay_factor_;
  double recover_step_;
  double current_;
};

struct PlaceLimiterSnapshot {
  int used_in_window = 0;
  int max_in_window = 0;
  double window_sec = 0.0;
  double min_interval_sec = 0.0;
  double wait_sec = 0.0;
};

// Mirrors Python opend_gateway.rate_limiter.MoomooPlaceOrderLimiter.
class MoomooPlaceOrderLimiter {
 public:
  explicit MoomooPlaceOrderLimiter(MoomooRateConfig cfg = {});

  int EffectiveMax() const;

  double SecondsUntilAllowed(const std::string &acc_key, double now) const;
  // max_wait_sec < 0 means no cap (mirrors Python max_wait_sec=None).
  double WaitUntilAllowed(const std::string &acc_key, double max_wait_sec = -1.0);

  // now < 0 uses steady clock (for tests pass explicit now).
  void OnFrequencyError(const std::string &acc_key, double backoff_sec = 1.0, double now = -1.0);
  PlaceLimiterSnapshot Snapshot(const std::string &acc_key, double now) const;

  // Records a successful place at `now` (also used internally by WaitUntilAllowed).
  void RecordPlace(const std::string &acc_key, double now);

 private:
  using TimestampDeque = std::deque<double>;

  void Prune(const std::string &acc_key, double now) const;
  double SecondsUntilAllowedUnlocked(const std::string &acc_key, double now) const;
  void RecordPlaceUnlocked(const std::string &acc_key, double now);

  static std::string NormalizeAccKey(const std::string &acc_key);
  static double NowMono();

  MoomooRateConfig cfg_;
  mutable std::mutex mu_;
  mutable std::unordered_map<std::string, TimestampDeque> timestamps_;
  mutable std::unordered_map<std::string, double> last_mono_;
  mutable std::unordered_map<std::string, double> cooldown_until_;
};

}  // namespace common
}  // namespace mmtrade
