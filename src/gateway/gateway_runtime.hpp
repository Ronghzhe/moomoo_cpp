#pragma once

#include "common/rate_limiter.hpp"
#include "gateway/gateway_config.hpp"
#include "gateway/request_scheduler.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

namespace mmtrade {
namespace opend {
class OrderClient;
class TradeConnection;
}

namespace gateway {

class GatewayRuntime {
 public:
  explicit GatewayRuntime(const GatewayConfig &cfg);
  ~GatewayRuntime();

  nlohmann::json Submit(const nlohmann::json &payload);
  nlohmann::json Metrics() const;
  void Close();

  const GatewayConfig &config() const { return cfg_; }

 private:
  struct Pending {
    nlohmann::json payload;
    double enqueued_mono;
    std::mutex mu;
    std::condition_variable cv;
    bool done;
    nlohmann::json result;

    Pending() : enqueued_mono(0.0), done(false) {}
  };

  void WorkerLoop();
  nlohmann::json ExecutePlace(const nlohmann::json &payload, double enqueued_mono,
                              double start_mono, const std::string &acc_id);
  std::string ResolveAccId(const nlohmann::json &payload) const;
  void ResolveRoute(const nlohmann::json &payload, std::string *host, int *port,
                    std::string *trd_env, std::string *trd_market, std::uint64_t *acc_id,
                    int *acc_index) const;
  opend::OrderClient *GetClient(const nlohmann::json &payload);
  bool EnsureSharedTradeConnected(const std::string &host, int port);
  nlohmann::json CacheGet(const std::string &request_id);
  void CachePut(const std::string &request_id, const nlohmann::json &result);
  void SetResult(const std::string &request_id, const nlohmann::json &result);
  void LogEvent(const nlohmann::json &event);

  GatewayConfig cfg_;
  RequestScheduler scheduler_;
  mutable std::mutex pending_mu_;
  std::unordered_map<std::string, std::shared_ptr<Pending> > pending_;
  mutable std::mutex done_mu_;
  std::unordered_map<std::string, std::pair<double, nlohmann::json> > done_cache_;
  common::MoomooPlaceOrderLimiter place_limiter_;
  std::atomic<bool> running_;
  std::thread worker_;
  mutable std::mutex client_mu_;
  std::unique_ptr<opend::TradeConnection> shared_trade_;
  std::string shared_trade_host_;
  int shared_trade_port_;
  std::map<std::string, std::unique_ptr<opend::OrderClient> > client_pool_;
  mutable std::mutex event_log_mu_;
  std::string event_log_path_;
  mutable std::mutex stats_mu_;
  double accepted_total_;
  double ok_total_;
  double error_total_;
  double freq_error_total_;
  double rate_wait_total_sec_;
  double avg_queue_wait_ms_;
  double avg_exec_ms_;
};

}  // namespace gateway
}  // namespace mmtrade
