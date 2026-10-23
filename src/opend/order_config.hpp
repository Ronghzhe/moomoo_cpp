#pragma once

#include "trade/order_parse.hpp"

#include <cstdint>
#include <string>

namespace mmtrade {
namespace opend {

struct OrderConfig {
  std::string host;
  int port;
  trade::TrdMarket trd_market;
  bool simulate;
  std::uint64_t acc_id;
  bool refresh_cache;

  OrderConfig()
      : host("127.0.0.1"),
        port(11121),
        trd_market(trade::TrdMarket::kUS),
        simulate(true),
        acc_id(0),
        refresh_cache(true) {}
};

struct PlaceOrderParams {
  std::string code_full;
  trade::TrdSide side;
  double qty;
  double price;
  trade::OrderType order_type;
  std::string remark;
  trade::TimeInForce time_in_force;
  bool fill_outside_rth;
  // Empty = do not set OpenD session (explicit NONE). Otherwise RTH/ALL/OVERNIGHT.
  std::string sdk_session;

  PlaceOrderParams()
      : side(trade::TrdSide::kBuy),
        qty(0.0),
        price(0.0),
        order_type(trade::OrderType::kNormal),
        time_in_force(trade::TimeInForce::kDay),
        fill_outside_rth(false) {}
};

struct PlaceOrderResult {
  bool ok;
  int ret_code;
  std::uint64_t order_id;
  std::string order_id_ex;
  std::string error;
  std::string raw_response;
  std::string response_source;
  int64_t pre_sdk_us;
  int64_t sdk_us;
  int64_t post_sdk_us;
  int64_t gateway_queue_wait_us;
  int64_t gateway_rate_wait_us;
  int64_t gateway_http_rtt_us;
  std::string place_via;

  PlaceOrderResult()
      : ok(false),
        ret_code(-1),
        order_id(0),
        pre_sdk_us(0),
        sdk_us(0),
        post_sdk_us(0),
        gateway_queue_wait_us(0),
        gateway_rate_wait_us(0),
        gateway_http_rtt_us(0) {}
};

}  // namespace opend
}  // namespace mmtrade
