#include "trade/order_push_cache.hpp"

#include <algorithm>
#include <thread>

namespace mmtrade {
namespace trade {

double OrderPushCache::NowMono() {
  typedef std::chrono::steady_clock Clock;
  return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

void OrderPushCache::Ingest(const OrderPushRow &row) {
  const std::string oid = row.order_id;
  if (oid.empty()) {
    return;
  }
  {
    std::lock_guard<std::mutex> lk(mu_);
    OrderPushRow merged = row;
    std::unordered_map<std::string, OrderPushRow>::iterator it = rows_.find(oid);
    if (it != rows_.end())
    {
      if (merged.create_ts_ms <= 0)
      {
        merged.create_ts_ms = it->second.create_ts_ms;
      }
      if (merged.fill_ts_ms <= 0)
      {
        merged.fill_ts_ms = it->second.fill_ts_ms;
      }
    }
    rows_[oid] = merged;
    versions_[oid] = versions_[oid] + 1;
    last_mono_[oid] = NowMono();
  }
  cv_.notify_all();
}

void OrderPushCache::Erase(const std::string &order_id) {
  if (order_id.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lk(mu_);
  rows_.erase(order_id);
  versions_.erase(order_id);
  last_mono_.erase(order_id);
}

bool OrderPushCache::GetRow(const std::string &order_id, OrderPushRow *out) const {
  if (out == NULL) {
    return false;
  }
  std::lock_guard<std::mutex> lk(mu_);
  std::unordered_map<std::string, OrderPushRow>::const_iterator it = rows_.find(order_id);
  if (it == rows_.end()) {
    return false;
  }
  *out = it->second;
  return true;
}

std::uint64_t OrderPushCache::GetVersion(const std::string &order_id) const {
  std::lock_guard<std::mutex> lk(mu_);
  std::unordered_map<std::string, std::uint64_t>::const_iterator it = versions_.find(order_id);
  if (it == versions_.end()) {
    return 0;
  }
  return it->second;
}

double OrderPushCache::LastUpdateMono(const std::string &order_id) const {
  std::lock_guard<std::mutex> lk(mu_);
  std::unordered_map<std::string, double>::const_iterator it = last_mono_.find(order_id);
  if (it == last_mono_.end()) {
    return 0.0;
  }
  return it->second;
}

bool OrderPushCache::WaitUpdate(const std::string &order_id, std::uint64_t since_version,
                                double timeout_sec, OrderPushRow *out) {
  if (out == NULL) {
    return false;
  }
  const double deadline = NowMono() + (timeout_sec > 0.0 ? timeout_sec : 0.0);
  while (true) {
    {
      std::lock_guard<std::mutex> lk(mu_);
      std::unordered_map<std::string, std::uint64_t>::const_iterator vit = versions_.find(order_id);
      if (vit != versions_.end() && vit->second > since_version) {
        std::unordered_map<std::string, OrderPushRow>::const_iterator rit = rows_.find(order_id);
        if (rit != rows_.end()) {
          *out = rit->second;
          return true;
        }
      }
      if (NowMono() >= deadline) {
        std::unordered_map<std::string, OrderPushRow>::const_iterator rit = rows_.find(order_id);
        if (rit != rows_.end()) {
          *out = rit->second;
        }
        return false;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

}  // namespace trade
}  // namespace mmtrade
