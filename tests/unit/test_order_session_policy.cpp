#include "strategy/order_session_policy.hpp"

#include <gtest/gtest.h>

using mmtrade::strategy::ClampOrderTypeForSession;
using mmtrade::strategy::EffectiveAutoStopAfterClose;
using mmtrade::strategy::MarketPhaseFromStateCode;
using mmtrade::strategy::MarketPhaseFromStateName;
using mmtrade::strategy::PhaseAllowsTrading;
using mmtrade::strategy::RetryExceededFatalCode;
using mmtrade::strategy::SessionAllowsMarketOrder;

TEST(OrderSessionPolicy, PhaseFromNamesMatchesPython) {
  EXPECT_EQ(MarketPhaseFromStateName("MORNING"), "rth");
  EXPECT_EQ(MarketPhaseFromStateName("AFTERNOON"), "rth");
  EXPECT_EQ(MarketPhaseFromStateName("REST"), "pre");
  EXPECT_EQ(MarketPhaseFromStateName("PRE_MARKET_BEGIN"), "pre");
  EXPECT_EQ(MarketPhaseFromStateName("WAITING_OPEN"), "pre");
  EXPECT_EQ(MarketPhaseFromStateName("OVERNIGHT"), "overnight");
  EXPECT_EQ(MarketPhaseFromStateName("AFTER_HOURS_BEGIN"), "post");
  EXPECT_EQ(MarketPhaseFromStateName("CLOSED"), "post");
  EXPECT_EQ(MarketPhaseFromStateName("HK_CAS"), "post");
  EXPECT_EQ(MarketPhaseFromStateName(""), "unknown");
}

TEST(OrderSessionPolicy, PhaseFromStateCode) {
  EXPECT_EQ(MarketPhaseFromStateCode(3), "rth");
  EXPECT_EQ(MarketPhaseFromStateCode(5), "rth");
  EXPECT_EQ(MarketPhaseFromStateCode(4), "pre");
  EXPECT_EQ(MarketPhaseFromStateCode(8), "pre");
  EXPECT_EQ(MarketPhaseFromStateCode(37), "overnight");
  EXPECT_EQ(MarketPhaseFromStateCode(33), "overnight");
  EXPECT_EQ(MarketPhaseFromStateCode(6), "post");
  EXPECT_EQ(MarketPhaseFromStateCode(10), "post");
  EXPECT_EQ(MarketPhaseFromStateCode(0), "unknown");
}

TEST(OrderSessionPolicy, PhaseAllowsTrading) {
  EXPECT_TRUE(PhaseAllowsTrading("RTH", "rth"));
  EXPECT_FALSE(PhaseAllowsTrading("RTH", "post"));
  EXPECT_TRUE(PhaseAllowsTrading("ALL", "pre"));
  EXPECT_TRUE(PhaseAllowsTrading("ALL", "rth"));
  EXPECT_TRUE(PhaseAllowsTrading("ALL", "post"));
  EXPECT_FALSE(PhaseAllowsTrading("ALL", "overnight"));
  EXPECT_TRUE(PhaseAllowsTrading("OVERNIGHT", "overnight"));
  EXPECT_TRUE(PhaseAllowsTrading("OVERNIGHT", "post"));
  EXPECT_FALSE(PhaseAllowsTrading("OVERNIGHT", "rth"));
  EXPECT_TRUE(PhaseAllowsTrading("NONE", "unknown"));
}

TEST(OrderSessionPolicy, MarketOnlyInRthClock) {
  EXPECT_TRUE(SessionAllowsMarketOrder("ALL", "rth"));
  EXPECT_FALSE(SessionAllowsMarketOrder("ALL", "post"));
  EXPECT_FALSE(SessionAllowsMarketOrder("ALL", ""));
  EXPECT_FALSE(SessionAllowsMarketOrder("OVERNIGHT", "rth"));
  EXPECT_EQ(ClampOrderTypeForSession("ALL", "MARKET", "post"), "NORMAL");
  EXPECT_EQ(ClampOrderTypeForSession("ALL", "MARKET", "rth"), "MARKET");
}

TEST(OrderSessionPolicy, RetryExceededCodes) {
  EXPECT_EQ(RetryExceededFatalCode("RTH", "rth", "entry"), "ENTRY_RETRY_EXCEEDED");
  EXPECT_EQ(RetryExceededFatalCode("ALL", "rth", "entry"), "ENTRY_RETRY_EXCEEDED_RTH");
  EXPECT_EQ(RetryExceededFatalCode("ALL", "post", "entry"), "ENTRY_RETRY_EXCEEDED_OVERNIGHT");
  EXPECT_EQ(RetryExceededFatalCode("ALL", "", "exit"), "EXIT_RETRY_EXCEEDED_OVERNIGHT");
}

TEST(OrderSessionPolicy, AutoStopOnlyRthSession) {
  EXPECT_TRUE(EffectiveAutoStopAfterClose(true, "RTH"));
  EXPECT_FALSE(EffectiveAutoStopAfterClose(true, "ALL"));
  EXPECT_FALSE(EffectiveAutoStopAfterClose(true, "OVERNIGHT"));
  EXPECT_FALSE(EffectiveAutoStopAfterClose(false, "RTH"));
}
