#include "trade/order_parse.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

using mmtrade::trade::CoerceTrdMarket;
using mmtrade::trade::OrderType;
using mmtrade::trade::ParseOrderType;
using mmtrade::trade::ParseTimeInForce;
using mmtrade::trade::ParseTrdSide;
using mmtrade::trade::TimeInForce;
using mmtrade::trade::TrdMarket;
using mmtrade::trade::TrdSide;

// Mirrors strategy/tests/test_order_client_coerce.py
TEST(OrderParse, CoerceTrdMarketStringUsHk) {
  EXPECT_EQ(CoerceTrdMarket("US"), TrdMarket::kUS);
  EXPECT_EQ(CoerceTrdMarket("hk"), TrdMarket::kHK);
}

TEST(OrderParse, CoerceTrdMarketAliases) {
  EXPECT_EQ(CoerceTrdMarket("USA"), TrdMarket::kUS);
  EXPECT_EQ(CoerceTrdMarket("HKG"), TrdMarket::kHK);
  EXPECT_EQ(CoerceTrdMarket("TRD_MARKET.HK"), TrdMarket::kHK);
  EXPECT_EQ(CoerceTrdMarket("CN"), TrdMarket::kCN);
  EXPECT_EQ(CoerceTrdMarket("SG"), TrdMarket::kSG);
  EXPECT_EQ(CoerceTrdMarket(""), TrdMarket::kUS);
}

TEST(OrderParse, ParseTrdSideBuySell) {
  EXPECT_EQ(ParseTrdSide("BUY"), TrdSide::kBuy);
  EXPECT_EQ(ParseTrdSide("sell"), TrdSide::kSell);
  EXPECT_EQ(ParseTrdSide("B"), TrdSide::kBuy);
  EXPECT_EQ(ParseTrdSide("S"), TrdSide::kSell);
}

TEST(OrderParse, ParseTrdSideInvalid) {
  EXPECT_THROW(ParseTrdSide("HOLD"), std::invalid_argument);
}

TEST(OrderParse, ParseOrderType) {
  EXPECT_EQ(ParseOrderType("NORMAL"), OrderType::kNormal);
  EXPECT_EQ(ParseOrderType("LIMIT"), OrderType::kNormal);
  EXPECT_EQ(ParseOrderType("SIM_MARKET"), OrderType::kNormal);
  EXPECT_EQ(ParseOrderType("MARKET"), OrderType::kMarket);
}

TEST(OrderParse, ParseOrderTypeInvalid) {
  EXPECT_THROW(ParseOrderType("STOP"), std::invalid_argument);
}

TEST(OrderParse, ParseTimeInForce) {
  EXPECT_EQ(ParseTimeInForce("DAY"), TimeInForce::kDay);
  EXPECT_EQ(ParseTimeInForce("gtc"), TimeInForce::kGtc);
}

TEST(OrderParse, ParseTimeInForceInvalid) {
  EXPECT_THROW(ParseTimeInForce("IOC"), std::invalid_argument);
}
