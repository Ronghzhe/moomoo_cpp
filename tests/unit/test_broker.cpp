#include "trade/broker_mapping.hpp"
#include "trade/broker_types.hpp"
#include "trade/order_id_util.hpp"

#include <gtest/gtest.h>

using mmtrade::trade::BrokerContext;
using mmtrade::trade::DirectBrokerConfig;
using mmtrade::trade::ExtractOrderIdString;
using mmtrade::trade::FormatOrderId;
using mmtrade::trade::ParseOrderIdUint64;
using mmtrade::trade::ResolveDisplayOrderId;
using mmtrade::trade::MakeBrokerFailure;
using mmtrade::trade::MakeBrokerSuccess;
using mmtrade::trade::MapPlaceSnapshot;
using mmtrade::trade::OpenDPlaceSnapshot;
using mmtrade::trade::PlaceOrderRequest;

TEST(BrokerTypes, BrokerContextDefaults) {
  BrokerContext ctx;
  EXPECT_EQ(ctx.acc_index, "0");
  EXPECT_EQ(ctx.trd_env, "SIMULATE");
  EXPECT_EQ(ctx.trd_market, "US");
  EXPECT_EQ(ctx.opend_host, "127.0.0.1");
  EXPECT_EQ(ctx.opend_port, 11121);
  EXPECT_EQ(ctx.priority, 50);
}

TEST(BrokerTypes, DirectBrokerConfigDefaults) {
  DirectBrokerConfig cfg;
  EXPECT_EQ(cfg.host, "127.0.0.1");
  EXPECT_EQ(cfg.port, 11121);
  EXPECT_TRUE(cfg.simulate);
  EXPECT_EQ(cfg.trd_market, mmtrade::trade::TrdMarket::kUS);
  EXPECT_EQ(cfg.acc_id, 0u);
  EXPECT_EQ(cfg.acc_index, 0);
  EXPECT_FALSE(cfg.trd_env_auto);
  EXPECT_TRUE(cfg.refresh_cache);
}

TEST(BrokerTypes, PlaceOrderRequestDefaults) {
  PlaceOrderRequest req;
  EXPECT_EQ(req.order_type, "NORMAL");
  EXPECT_EQ(req.time_in_force, "DAY");
  EXPECT_FALSE(req.fill_outside_rth);
}

TEST(BrokerTypes, MakeBrokerSuccessAndFailure) {
  const mmtrade::trade::PlaceOrderResult ok = MakeBrokerSuccess("12345", "SUBMITTING");
  EXPECT_TRUE(ok.ok);
  EXPECT_EQ(ok.ret_code, 0);
  EXPECT_EQ(ok.order_id, "12345");
  EXPECT_EQ(ok.status, "SUBMITTING");

  const mmtrade::trade::PlaceOrderResult fail = MakeBrokerFailure(3, "rate limit");
  EXPECT_FALSE(fail.ok);
  EXPECT_EQ(fail.ret_code, 3);
  EXPECT_EQ(fail.error, "rate limit");
}

TEST(OrderIdUtil, FormatOrderId) {
  EXPECT_EQ(FormatOrderId(0), "");
  EXPECT_EQ(FormatOrderId(9876543210ULL), "9876543210");
}

TEST(OrderIdUtil, ResolveDisplayOrderIdPrefersOrderIdEx) {
  EXPECT_EQ(ResolveDisplayOrderId(2705422575547576058ULL, "3233923"), "3233923");
  EXPECT_EQ(ResolveDisplayOrderId(4242, ""), "4242");
  EXPECT_EQ(ResolveDisplayOrderId(0, ""), "");
}

TEST(OrderIdUtil, ExtractOrderIdStringNumeric) {
  EXPECT_EQ(ExtractOrderIdString("123456"), "123456");
  EXPECT_EQ(ExtractOrderIdString(""), "");
}

TEST(OrderIdUtil, ExtractOrderIdStringJsonLike) {
  const std::string payload = "{\"order_id\":\"OID-9\",\"status\":\"SUBMITTING\"}";
  EXPECT_EQ(ExtractOrderIdString(payload), "OID-9");
}

TEST(OrderIdUtil, ParseOrderIdUint64) {
  EXPECT_EQ(ParseOrderIdUint64("3238221"), 3238221ULL);
  EXPECT_EQ(ParseOrderIdUint64("2705422575547576058"), 2705422575547576058ULL);
  EXPECT_EQ(ParseOrderIdUint64(""), 0u);
  EXPECT_EQ(ParseOrderIdUint64("OID-9"), 0u);
}

TEST(DirectBrokerMapping, MapsSuccessfulOpenDResult) {
  OpenDPlaceSnapshot raw;
  raw.ok = true;
  raw.ret_code = 0;
  raw.order_id = 4242;
  const mmtrade::trade::PlaceOrderResult mapped = MapPlaceSnapshot(raw);
  EXPECT_TRUE(mapped.ok);
  EXPECT_EQ(mapped.order_id, "4242");
}

TEST(DirectBrokerMapping, PrefersOrderIdExForDisplay) {
  OpenDPlaceSnapshot raw;
  raw.ok = true;
  raw.ret_code = 0;
  raw.order_id = 2705422575547576058ULL;
  raw.order_id_ex = "3233923";
  const mmtrade::trade::PlaceOrderResult mapped = MapPlaceSnapshot(raw);
  EXPECT_TRUE(mapped.ok);
  EXPECT_EQ(mapped.order_id, "3233923");
  EXPECT_EQ(mapped.internal_order_id, 2705422575547576058ULL);
}

TEST(DirectBrokerMapping, MapsFailedOpenDResult) {
  OpenDPlaceSnapshot raw;
  raw.ok = false;
  raw.ret_code = 3;
  raw.error = "high frequency";
  const mmtrade::trade::PlaceOrderResult mapped = MapPlaceSnapshot(raw);
  EXPECT_FALSE(mapped.ok);
  EXPECT_EQ(mapped.ret_code, 3);
  EXPECT_EQ(mapped.error, "high frequency");
}

TEST(DirectBrokerMapping, MissingOrderIdIsFailure) {
  OpenDPlaceSnapshot raw;
  raw.ok = true;
  raw.ret_code = 0;
  raw.order_id = 0;
  const mmtrade::trade::PlaceOrderResult mapped = MapPlaceSnapshot(raw);
  EXPECT_FALSE(mapped.ok);
  EXPECT_EQ(mapped.error, "missing order_id");
}

TEST(DirectBrokerMapping, OrderIdExOnlyIsSuccess) {
  OpenDPlaceSnapshot raw;
  raw.ok = true;
  raw.ret_code = 0;
  raw.order_id = 0;
  raw.order_id_ex = "3233923";
  const mmtrade::trade::PlaceOrderResult mapped = MapPlaceSnapshot(raw);
  EXPECT_TRUE(mapped.ok);
  EXPECT_EQ(mapped.order_id, "3233923");
}
