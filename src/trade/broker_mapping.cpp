#include "trade/broker_mapping.hpp"

#include "trade/order_id_util.hpp"

namespace mmtrade {
namespace trade {

PlaceOrderResult MapPlaceSnapshot(const OpenDPlaceSnapshot &raw) {
  if (!raw.ok) {
    PlaceOrderResult out = MakeBrokerFailure(raw.ret_code, raw.error);
    out.place_via = "direct";
    return out;
  }
  const std::string order_id = ResolveDisplayOrderId(raw.order_id, raw.order_id_ex);
  if (order_id.empty()) {
    PlaceOrderResult out = MakeBrokerFailure(-1, "missing order_id");
    out.place_via = "direct";
    return out;
  }
  PlaceOrderResult out = MakeBrokerSuccess(order_id);
  out.internal_order_id = raw.order_id;
  out.place_via = "direct";
  return out;
}

}  // namespace trade
}  // namespace mmtrade
