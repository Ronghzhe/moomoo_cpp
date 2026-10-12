#include "gateway/request_scheduler.hpp"
#include "trade/gateway_http.hpp"

#include <gtest/gtest.h>

using mmtrade::gateway::RequestScheduler;
using mmtrade::trade::MakeBrokerFailure;
using mmtrade::trade::PlaceOrderResult;
using mmtrade::trade::ShouldFallbackToDirect;
using mmtrade::trade::IsGatewayInflightTimeout;
using mmtrade::trade::IsTransientGatewayInfraFailure;

TEST(RequestScheduler, PriorityThenFifo) {
  RequestScheduler sched(100);
  ASSERT_TRUE(sched.TryPut("low", 50));
  ASSERT_TRUE(sched.TryPut("high", 10));
  ASSERT_TRUE(sched.TryPut("low2", 50));
  mmtrade::gateway::ScheduledRequest a;
  mmtrade::gateway::ScheduledRequest b;
  mmtrade::gateway::ScheduledRequest c;
  ASSERT_TRUE(sched.Get(&a, 0.1));
  ASSERT_TRUE(sched.Get(&b, 0.1));
  ASSERT_TRUE(sched.Get(&c, 0.1));
  EXPECT_EQ(a.request_id, "high");
  EXPECT_EQ(b.request_id, "low");
  EXPECT_EQ(c.request_id, "low2");
}

TEST(RequestScheduler, QueueFull) {
  RequestScheduler sched(1);
  ASSERT_TRUE(sched.TryPut("a", 50));
  ASSERT_FALSE(sched.TryPut("b", 50));
}

TEST(GatewayHttpHelpers, InflightTimeoutNoFallback) {
  PlaceOrderResult res = MakeBrokerFailure(-1, "gateway timeout waiting result");
  EXPECT_TRUE(IsGatewayInflightTimeout(res));
  EXPECT_FALSE(ShouldFallbackToDirect(res));
}

TEST(GatewayHttpHelpers, UnreachableFallback) {
  PlaceOrderResult res = MakeBrokerFailure(-1, "gateway_unreachable: connect failed");
  EXPECT_TRUE(ShouldFallbackToDirect(res));
  EXPECT_TRUE(IsTransientGatewayInfraFailure(res));
}

TEST(GatewayHttpHelpers, BrokerRejectNoFallback) {
  PlaceOrderResult res = MakeBrokerFailure(3, "some broker reject");
  EXPECT_FALSE(ShouldFallbackToDirect(res));
}
