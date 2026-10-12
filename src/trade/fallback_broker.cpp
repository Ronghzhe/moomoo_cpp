#include "trade/fallback_broker.hpp"

#include "trade/gateway_http.hpp"

#include <chrono>
#include <cstdlib>
#include <thread>

namespace mmtrade {
namespace trade {

namespace {

int TransientRetryMax() {
  const char *raw = std::getenv("STRATEGY_GATEWAY_TRANSIENT_RETRY_MAX");
  if (raw == NULL || raw[0] == '\0') {
    return 2;
  }
  const int v = std::atoi(raw);
  return v < 0 ? 0 : v;
}

double TransientRetryWaitSec() {
  const char *raw = std::getenv("STRATEGY_GATEWAY_TRANSIENT_RETRY_WAIT_SEC");
  if (raw == NULL || raw[0] == '\0') {
    return 0.35;
  }
  const double v = std::atof(raw);
  return v < 0.0 ? 0.0 : v;
}

}  // namespace

FallbackBroker::FallbackBroker(std::unique_ptr<IOrderBroker> gateway,
                               std::unique_ptr<IOrderBroker> direct)
    : gateway_(std::move(gateway)), direct_(std::move(direct)) {}

FallbackBroker::~FallbackBroker() { Close(); }

void FallbackBroker::Close() {
  if (gateway_.get() != NULL) {
    gateway_->Close();
  }
  if (direct_.get() != NULL) {
    direct_->Close();
  }
}

PlaceOrderResult FallbackBroker::PlaceOrder(const PlaceOrderRequest &req) {
  const int retry_max = TransientRetryMax();
  const double wait_sec = TransientRetryWaitSec();
  PlaceOrderResult gw_res;
  for (int attempt = 0; attempt <= retry_max; ++attempt) {
    gw_res = gateway_->PlaceOrder(req);
    if (!ShouldFallbackToDirect(gw_res)) {
      return gw_res;
    }
    if (attempt < retry_max && IsTransientGatewayInfraFailure(gw_res)) {
      if (wait_sec > 0.0) {
        std::this_thread::sleep_for(std::chrono::duration<double>(wait_sec));
      }
      continue;
    }
    break;
  }
  PlaceOrderResult direct_res = direct_->PlaceOrder(req);
  if (direct_res.place_via.empty() || direct_res.place_via == "direct") {
    direct_res.place_via = "direct_fallback";
  }
  return direct_res;
}

}  // namespace trade
}  // namespace mmtrade
