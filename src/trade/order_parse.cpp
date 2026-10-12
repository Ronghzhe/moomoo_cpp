#include "trade/order_parse.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace mmtrade {
namespace trade {
namespace {

std::string UpperTrim(const std::string &s) {
  std::string out = s;
  out.erase(out.begin(), std::find_if(out.begin(), out.end(),
                                      [](unsigned char c) { return !std::isspace(c); }));
  out.erase(std::find_if(out.rbegin(), out.rend(),
                         [](unsigned char c) { return !std::isspace(c); })
                .base(),
            out.end());
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return out;
}

bool IsOneOf(const std::string &s, const char *a, const char *b) {
  return s == a || s == b;
}

bool IsOneOf4(const std::string &s, const char *a, const char *b, const char *c, const char *d) {
  return s == a || s == b || s == c || s == d;
}

}  // namespace

TrdMarket CoerceTrdMarket(const std::string &value) {
  const std::string s = UpperTrim(value);
  if (s.empty()) {
    return TrdMarket::kUS;
  }
  if (IsOneOf4(s, "US", "USA", "TRD_MARKET.US", "TRDMARKET.US")) {
    return TrdMarket::kUS;
  }
  if (IsOneOf4(s, "HK", "HKG", "TRD_MARKET.HK", "TRDMARKET.HK")) {
    return TrdMarket::kHK;
  }
  if (IsOneOf4(s, "CN", "SH", "SZ", "TRD_MARKET.CN") || s == "TRDMARKET.CN") {
    return TrdMarket::kCN;
  }
  if (IsOneOf(s, "SG", "TRD_MARKET.SG") || s == "TRDMARKET.SG") {
    return TrdMarket::kSG;
  }
  return TrdMarket::kUS;
}

TrdSide ParseTrdSide(const std::string &side) {
  const std::string s = UpperTrim(side);
  if (IsOneOf(s, "BUY", "B")) {
    return TrdSide::kBuy;
  }
  if (IsOneOf(s, "SELL", "S")) {
    return TrdSide::kSell;
  }
  throw std::invalid_argument("trd_side must be BUY/SELL, got: " + side);
}

OrderType ParseOrderType(const std::string &order_type) {
  const std::string s = UpperTrim(order_type);
  if (s == "NORMAL" || s == "LIMIT" || s == "SIM_MARKET") {
    return OrderType::kNormal;
  }
  if (s == "MARKET") {
    return OrderType::kMarket;
  }
  throw std::invalid_argument(
      "order_type must be NORMAL/LIMIT/SIM_MARKET/MARKET, got: " + order_type);
}

TimeInForce ParseTimeInForce(const std::string &tif) {
  const std::string s = UpperTrim(tif);
  if (s == "DAY") {
    return TimeInForce::kDay;
  }
  if (s == "GTC") {
    return TimeInForce::kGtc;
  }
  throw std::invalid_argument("time_in_force must be DAY/GTC, got: " + tif);
}

const char *TrdMarketName(TrdMarket market) {
  switch (market) {
    case TrdMarket::kUS:
      return "US";
    case TrdMarket::kHK:
      return "HK";
    case TrdMarket::kCN:
      return "CN";
    case TrdMarket::kSG:
      return "SG";
  }
  return "US";
}

const char *TrdSideName(TrdSide side) {
  return side == TrdSide::kBuy ? "BUY" : "SELL";
}

const char *OrderTypeName(OrderType order_type) {
  return order_type == OrderType::kNormal ? "NORMAL" : "MARKET";
}

const char *TimeInForceName(TimeInForce tif) {
  return tif == TimeInForce::kDay ? "DAY" : "GTC";
}

}  // namespace trade
}  // namespace mmtrade
