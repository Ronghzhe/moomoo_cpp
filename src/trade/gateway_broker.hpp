#pragma once

#include "trade/broker_types.hpp"
#include "trade/i_order_broker.hpp"

#include <string>

namespace mmtrade {
namespace trade {

struct GatewayBrokerConfig {
  std::string base_url;
  double timeout_sec;

  GatewayBrokerConfig() : base_url("http://127.0.0.1:18766"), timeout_sec(8.0) {}
};

// HTTP client for local C++ opend gateway (:18766 by default).
class GatewayBroker : public IOrderBroker {
 public:
  explicit GatewayBroker(const GatewayBrokerConfig &cfg);
  ~GatewayBroker();

  PlaceOrderResult PlaceOrder(const PlaceOrderRequest &req) override;
  void Close() override;

 private:
  GatewayBrokerConfig cfg_;
};

}  // namespace trade
}  // namespace mmtrade
