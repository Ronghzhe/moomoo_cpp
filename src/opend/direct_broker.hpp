#pragma once

#include "opend/order_config.hpp"
#include "trade/broker_types.hpp"
#include "trade/i_order_broker.hpp"

#include <memory>

namespace mmtrade {
namespace opend {

class OrderClient;

// Mirrors Python broker.direct_broker.DirectBroker.
class DirectBroker : public trade::IOrderBroker {
 public:
  explicit DirectBroker(const trade::DirectBrokerConfig &config);
  ~DirectBroker();

  trade::PlaceOrderResult PlaceOrder(const trade::PlaceOrderRequest &req) override;
  void Close() override;

  OrderClient *Client();
  const trade::DirectBrokerConfig &config() const { return config_; }

 private:
  trade::DirectBrokerConfig config_;
  std::unique_ptr<OrderClient> client_;
  bool connected_;
};

}  // namespace opend
}  // namespace mmtrade
