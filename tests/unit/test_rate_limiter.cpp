#include "common/rate_limiter.hpp"
#include "trade/order_errors.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>

using mmtrade::common::AdaptiveRate;
using mmtrade::common::MoomooPlaceOrderLimiter;
using mmtrade::common::MoomooRateConfig;
using mmtrade::common::TokenBucket;
using mmtrade::trade::IsCancelledAllDetail;
using mmtrade::trade::IsCancelledAllStatus;
using mmtrade::trade::IsOperatingTooFastErr;
using mmtrade::trade::IsOrderFailRetriable;
using mmtrade::trade::IsOrderRateLimitErr;
using mmtrade::trade::IsRetryableInvalidPriceFail;
using mmtrade::trade::IsRetryableLimitPriceFail;
using mmtrade::trade::IsRetryablePlaceNoOid;
using mmtrade::trade::IsRetryableTryAgainLater;
using mmtrade::trade::OrderFailDetailForLog;

// Mirrors strategy/tests/test_opend_gateway_components.py

TEST(TokenBucket, AllowAndRefill) {
  TokenBucket bucket(2.0, 2.0);
  EXPECT_TRUE(bucket.Allow());
  EXPECT_TRUE(bucket.Allow());
  EXPECT_FALSE(bucket.Allow());
}

TEST(AdaptiveRate, DecayAndRecover) {
  AdaptiveRate rate(10.0, 2.0);
  EXPECT_DOUBLE_EQ(rate.current(), 10.0);
  rate.OnFrequencyError();
  EXPECT_LT(rate.current(), 10.0);
  for (int i = 0; i < 10; ++i) {
    rate.OnFrequencyError();
  }
  EXPECT_GE(rate.current(), 2.0);
  for (int i = 0; i < 200; ++i) {
    rate.OnSuccessTick();
  }
  EXPECT_NEAR(rate.current(), 10.0, 1e-9);
}

TEST(MoomooPlaceOrderLimiter, ReserveSlots) {
  MoomooPlaceOrderLimiter lim(MoomooRateConfig{30.0, 15, 0.02, 1});
  EXPECT_EQ(lim.EffectiveMax(), 14);
}

TEST(MoomooPlaceOrderLimiter, MinIntervalDeterministic) {
  MoomooPlaceOrderLimiter lim(MoomooRateConfig{30.0, 15, 0.02, 0});
  lim.RecordPlace("0", 100.0);
  EXPECT_NEAR(lim.SecondsUntilAllowed("0", 100.01), 0.01, 1e-9);
  EXPECT_LE(lim.SecondsUntilAllowed("0", 100.02), 1e-9);
}

TEST(MoomooPlaceOrderLimiter, WindowCapDeterministic) {
  MoomooRateConfig cfg;
  cfg.window_sec = 0.2;
  cfg.max_requests = 3;
  cfg.reserve_slots = 0;
  cfg.min_interval_sec = 0.0;
  MoomooPlaceOrderLimiter lim(cfg);
  lim.RecordPlace("acc1", 10.0);
  lim.RecordPlace("acc1", 10.05);
  lim.RecordPlace("acc1", 10.1);
  EXPECT_NEAR(lim.SecondsUntilAllowed("acc1", 10.11), 0.09, 1e-9);
  EXPECT_DOUBLE_EQ(lim.SecondsUntilAllowed("acc1", 10.21), 0.0);
}

TEST(MoomooPlaceOrderLimiter, AccIdIsolation) {
  MoomooRateConfig cfg;
  cfg.window_sec = 30.0;
  cfg.max_requests = 2;
  cfg.reserve_slots = 0;
  cfg.min_interval_sec = 0.0;
  MoomooPlaceOrderLimiter lim(cfg);
  lim.RecordPlace("1", 50.0);
  lim.RecordPlace("2", 50.0);
  EXPECT_EQ(lim.Snapshot("1", 50.0).used_in_window, 1);
  EXPECT_EQ(lim.Snapshot("2", 50.0).used_in_window, 1);
}

TEST(MoomooPlaceOrderLimiter, MinIntervalWallClock) {
  MoomooPlaceOrderLimiter lim(MoomooRateConfig{30.0, 15, 0.02, 0});
  lim.WaitUntilAllowed("0");
  const auto t0 = std::chrono::steady_clock::now();
  lim.WaitUntilAllowed("0");
  const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  EXPECT_GE(elapsed, 0.019);
}

TEST(MoomooPlaceOrderLimiter, WaitMaxCap) {
  MoomooRateConfig cfg;
  cfg.window_sec = 30.0;
  cfg.max_requests = 1;
  cfg.reserve_slots = 0;
  cfg.min_interval_sec = 0.0;
  MoomooPlaceOrderLimiter lim(cfg);
  lim.WaitUntilAllowed("acc1");
  const auto t0 = std::chrono::steady_clock::now();
  const double waited = lim.WaitUntilAllowed("acc1", 0.15);
  const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  EXPECT_LE(waited, 0.2);
  EXPECT_LT(elapsed, 0.5);
}

TEST(MoomooPlaceOrderLimiter, FrequencyErrorCooldown) {
  MoomooPlaceOrderLimiter lim(MoomooRateConfig{30.0, 15, 0.0, 0});
  lim.OnFrequencyError("acc1", 1.0, 100.0);
  EXPECT_NEAR(lim.SecondsUntilAllowed("acc1", 100.5), 0.5, 1e-9);
  EXPECT_DOUBLE_EQ(lim.SecondsUntilAllowed("acc1", 101.0), 0.0);
}

// Mirrors strategy/tests/test_order_rate_limit_retry.py

TEST(OrderErrors, PlaceHighFrequency) {
  const std::string msg =
      "Place Order request failed due to high frequency. "
      "Maximum 15 times per 30 seconds.";
  EXPECT_TRUE(IsOrderRateLimitErr(msg));
}

TEST(OrderErrors, QueryTenPerWindow) {
  EXPECT_TRUE(IsOrderRateLimitErr("Maximum 10 times per 30 seconds"));
}

TEST(OrderErrors, NotBusinessError) {
  EXPECT_FALSE(IsOrderRateLimitErr("Short selling not supported"));
}

TEST(OrderErrors, OperatingTooFast) {
  EXPECT_TRUE(IsOperatingTooFastErr("Operating too fast"));
  EXPECT_TRUE(IsOperatingTooFastErr("操作过快"));
  EXPECT_FALSE(IsOperatingTooFastErr(""));
}

TEST(OrderErrors, LimitPriceFail) {
  EXPECT_TRUE(IsRetryableLimitPriceFail(
      "The limit price is much higher (for buys) / lower (for sells) than the market price."));
  EXPECT_TRUE(IsRetryableLimitPriceFail("order input price limit"));
  EXPECT_TRUE(IsRetryableLimitPriceFail("限价偏离市价过大"));
  EXPECT_FALSE(IsRetryableLimitPriceFail("Short selling not supported"));
}

TEST(OrderErrors, InvalidPriceFail) {
  EXPECT_TRUE(IsRetryableInvalidPriceFail("invalid price"));
  EXPECT_TRUE(IsRetryableInvalidPriceFail("Please enter the correct price"));
  EXPECT_TRUE(IsRetryableInvalidPriceFail("minimum tick size"));
  EXPECT_TRUE(IsRetryableInvalidPriceFail("价格无效"));
  EXPECT_FALSE(IsRetryableInvalidPriceFail("Operating too fast"));
}

TEST(OrderErrors, TryAgainLaterAndNoOid) {
  EXPECT_TRUE(IsRetryableTryAgainLater("Please try again later"));
  EXPECT_TRUE(IsRetryableTryAgainLater("请稍后再试"));
  EXPECT_TRUE(IsRetryablePlaceNoOid("place_ok_but_no_oid"));
  EXPECT_TRUE(IsRetryablePlaceNoOid("no_order_id"));
  EXPECT_FALSE(IsRetryablePlaceNoOid("other"));
}

TEST(OrderErrors, CancelledAllAndRetriable) {
  EXPECT_TRUE(IsCancelledAllStatus("CANCELLED_ALL"));
  EXPECT_TRUE(IsCancelledAllStatus("cancelled_all"));
  EXPECT_TRUE(IsCancelledAllDetail("order_cancelled_all"));
  EXPECT_TRUE(IsOrderFailRetriable("CANCELLED_ALL", ""));
  EXPECT_TRUE(IsOrderFailRetriable("", "Operating too fast"));
  EXPECT_TRUE(IsOrderFailRetriable(
      "", "Place Order request failed due to high frequency. Maximum 15 times per 30 seconds."));
  EXPECT_FALSE(IsOrderFailRetriable("FAILED", "Short selling not supported"));
}

TEST(OrderErrors, FailDetailForLog) {
  EXPECT_EQ(OrderFailDetailForLog("CANCELLED_ALL", ""), "order_cancelled_all(last_err_msg_empty)");
  EXPECT_EQ(OrderFailDetailForLog("X", "broker boom"), "broker boom");
  EXPECT_EQ(OrderFailDetailForLog("", ""), "-");
}
