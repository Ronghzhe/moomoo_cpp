#include "trade/broker_types.hpp"

#include "trade/order_id_util.hpp"

namespace mmtrade {
namespace trade {

PlaceOrderResult MakeBrokerFailure(int ret_code, const std::string &error) {
  return PlaceOrderResult(false, ret_code, "", "", error);
}

PlaceOrderResult MakeBrokerSuccess(const std::string &order_id, const std::string &status) {
  PlaceOrderResult out(true, 0, order_id, status, "", ParseOrderIdUint64(order_id));
  if (out.internal_order_id == 0) {
    out.internal_order_id = ParseOrderIdUint64(ExtractOrderIdString(order_id));
  }
  return out;
}

}  // namespace trade
}  // namespace mmtrade
