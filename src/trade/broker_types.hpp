#pragma once

#include "trade/order_parse.hpp"

#include <cstdint>
#include <string>

namespace mmtrade {
namespace trade {

struct BrokerContext {
  std::string strategy_id;
  std::string symbol;
  std::string account_key;
  std::string acc_id;
  std::string acc_index;
  std::string trd_env;
  std::string trd_market;
  std::string opend_host;
  int opend_port;
  int priority;

  BrokerContext()
      : acc_index("0"),
        trd_env("SIMULATE"),
        trd_market("US"),
        opend_host("127.0.0.1"),
        opend_port(11121),
        priority(50) {}
};

struct PlaceOrderRequest {
  std::string code;
  std::string side;
  double qty;
  double price;
  std::string order_type;
  std::string remark;
  bool fill_outside_rth;
  std::string time_in_force;
  std::string session;
  std::string request_id;
  BrokerContext context;

  PlaceOrderRequest()
      : qty(0.0),
        price(0.0),
        order_type("NORMAL"),
        fill_outside_rth(false),
        time_in_force("DAY") {}
};

struct PlaceOrderResult {
  bool ok;
  int ret_code;
  std::string order_id;
  std::uint64_t internal_order_id;
  std::string status;
  std::string error;
  // Optional place-path timings (us). Populated by gateway / direct as available.
  std::int64_t queue_wait_us;
  std::int64_t rate_wait_us;
  std::int64_t place_pre_us;
  std::int64_t place_sdk_us;
  std::int64_t place_post_us;
  std::int64_t http_rtt_us;
  std::string place_via;

  PlaceOrderResult()
      : ok(false),
        ret_code(-1),
        internal_order_id(0),
        queue_wait_us(0),
        rate_wait_us(0),
        place_pre_us(0),
        place_sdk_us(0),
        place_post_us(0),
        http_rtt_us(0) {}

  PlaceOrderResult(bool ok_in, int ret_code_in, const std::string &order_id_in = "",
                   const std::string &status_in = "", const std::string &error_in = "",
                   std::uint64_t internal_order_id_in = 0)
      : ok(ok_in),
        ret_code(ret_code_in),
        order_id(order_id_in),
        internal_order_id(internal_order_id_in),
        status(status_in),
        error(error_in),
        queue_wait_us(0),
        rate_wait_us(0),
        place_pre_us(0),
        place_sdk_us(0),
        place_post_us(0),
        http_rtt_us(0) {}
};

struct DirectBrokerConfig {
  std::string host;
  int port;
  bool simulate;
  TrdMarket trd_market;
  std::uint64_t acc_id;
  int acc_index;
  bool trd_env_auto;
  bool refresh_cache;

  DirectBrokerConfig()
      : host("127.0.0.1"),
        port(11121),
        simulate(true),
        trd_market(TrdMarket::kUS),
        acc_id(0),
        acc_index(0),
        trd_env_auto(false),
        refresh_cache(true) {}
};

PlaceOrderResult MakeBrokerFailure(int ret_code, const std::string &error);
PlaceOrderResult MakeBrokerSuccess(const std::string &order_id, const std::string &status = "");

}  // namespace trade
}  // namespace mmtrade
