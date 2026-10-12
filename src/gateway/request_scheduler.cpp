#include "gateway/request_scheduler.hpp"

#include <chrono>

namespace mmtrade {
namespace gateway {

namespace {

double SteadyNowSec() {
  using clock = std::chrono::steady_clock;
  return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

}  // namespace

ScheduledRequest::ScheduledRequest()
    : priority(50), seq(0), created_mono(0.0) {}

ScheduledRequest::ScheduledRequest(int priority_in, int seq_in, const std::string &request_id_in,
                                   double created_mono_in)
    : priority(priority_in),
      seq(seq_in),
      request_id(request_id_in),
      created_mono(created_mono_in) {}

bool ScheduledRequest::operator>(const ScheduledRequest &other) const {
  if (priority != other.priority) {
    return priority > other.priority;
  }
  return seq > other.seq;
}

RequestScheduler::RequestScheduler(std::size_t maxsize) : maxsize_(maxsize), seq_(0) {}

bool RequestScheduler::TryPut(const std::string &request_id, int priority) {
  std::lock_guard<std::mutex> lock(mu_);
  if (q_.size() >= maxsize_) {
    return false;
  }
  ++seq_;
  q_.push(ScheduledRequest(priority, seq_, request_id, SteadyNowSec()));
  cv_.notify_one();
  return true;
}

bool RequestScheduler::Get(ScheduledRequest *out, double timeout_sec) {
  if (out == NULL) {
    return false;
  }
  std::unique_lock<std::mutex> lock(mu_);
  if (timeout_sec <= 0.0) {
    if (q_.empty()) {
      return false;
    }
  } else {
    const bool ok = cv_.wait_for(lock, std::chrono::duration<double>(timeout_sec),
                                 [this]() { return !q_.empty(); });
    if (!ok || q_.empty()) {
      return false;
    }
  }
  *out = q_.top();
  q_.pop();
  return true;
}

std::size_t RequestScheduler::size() const {
  std::lock_guard<std::mutex> lock(mu_);
  return q_.size();
}

}  // namespace gateway
}  // namespace mmtrade
