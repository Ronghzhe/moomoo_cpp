#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

namespace mmtrade {
namespace gateway {

struct ScheduledRequest {
  int priority;
  int seq;
  std::string request_id;
  double created_mono;

  ScheduledRequest();
  ScheduledRequest(int priority_in, int seq_in, const std::string &request_id_in,
                   double created_mono_in);

  bool operator>(const ScheduledRequest &other) const;
};

// Priority queue with FIFO inside same priority. Mirrors Python RequestScheduler.
class RequestScheduler {
 public:
  explicit RequestScheduler(std::size_t maxsize = 2000);

  bool TryPut(const std::string &request_id, int priority);
  // Returns false on timeout with no item.
  bool Get(ScheduledRequest *out, double timeout_sec);
  std::size_t size() const;
  std::size_t maxsize() const { return maxsize_; }

 private:
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::priority_queue<ScheduledRequest, std::vector<ScheduledRequest>,
                      std::greater<ScheduledRequest> >
      q_;
  std::size_t maxsize_;
  int seq_;
};

}  // namespace gateway
}  // namespace mmtrade
