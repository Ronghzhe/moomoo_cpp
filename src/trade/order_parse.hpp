#pragma once

#include <string>

namespace mmtrade {
namespace trade {

enum class TrdMarket {
  kUS,
  kHK,
  kCN,
  kSG,
};

enum class TrdSide {
  kBuy,
  kSell,
};

enum class OrderType {
  kNormal,
  kMarket,
};

enum class TimeInForce {
  kDay,
  kGtc,
};

struct ParseError {
  std::string message;
};

// Mirrors Python order_client.coerce_trd_market
TrdMarket CoerceTrdMarket(const std::string &value);

// Mirrors Python parse_trd_side; throws std::invalid_argument on failure
TrdSide ParseTrdSide(const std::string &side);

// Mirrors Python parse_order_type
OrderType ParseOrderType(const std::string &order_type);

// Mirrors Python parse_time_in_force
TimeInForce ParseTimeInForce(const std::string &tif);

const char *TrdMarketName(TrdMarket market);
const char *TrdSideName(TrdSide side);
const char *OrderTypeName(OrderType order_type);
const char *TimeInForceName(TimeInForce tif);

}  // namespace trade
}  // namespace mmtrade
