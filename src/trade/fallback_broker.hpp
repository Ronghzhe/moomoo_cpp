#pragma once

#include "trade/i_order_broker.hpp"

#include <memory>

namespace mmtrade {
namespace trade {

// Gateway first; on infrastructure failure, short retry then DirectBroker fallback.
// Inflight timeout / broker reject (incl. rate limit) never fall back — avoid double place.
class FallbackBroker : public IOrderBroker {
 public:
  FallbackBroker(std::unique_ptr<IOrderBroker> gateway, std::unique_ptr<IOrderBroker> direct);
  ~FallbackBroker();

  PlaceOrderResult PlaceOrder(const PlaceOrderRequest &req) override;
  void Close() override;

 private:
  std::unique_ptr<IOrderBroker> gateway_;
  std::unique_ptr<IOrderBroker> direct_;
};

}  // namespace trade
}  // namespace mmtrade
