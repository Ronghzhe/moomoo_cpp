#pragma once

#include "trade/broker_types.hpp"

namespace mmtrade {
namespace trade {

class IOrderBroker {
 public:
  virtual ~IOrderBroker() {}

  virtual PlaceOrderResult PlaceOrder(const PlaceOrderRequest &req) = 0;
  virtual void Close() = 0;
};

}  // namespace trade
}  // namespace mmtrade
