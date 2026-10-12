#include "strategy/net_split_leg_wait.hpp"

#include <gtest/gtest.h>

using mmtrade::strategy::IsStaleWaitStatus;
using mmtrade::strategy::NetSplitPendingSnapshot;
using mmtrade::strategy::ShouldNetSplitBookGuardStaleReprice;
using mmtrade::strategy::ShouldNetSplitStaleReprice;

TEST(NetSplitLegWait, StaleWaitStatusRecognized) {
  EXPECT_TRUE(IsStaleWaitStatus("SUBMITTED"));
  EXPECT_TRUE(IsStaleWaitStatus("SUBMITTING"));
  EXPECT_FALSE(IsStaleWaitStatus("FILLED_ALL"));
}

TEST(NetSplitLegWait, BookGuardStaleTriggersReprice) {
  NetSplitPendingSnapshot pend;
  pend.book_guard_emergency = true;
  pend.book_guard_emergency_limit = true;
  pend.book_guard_emergency_market_tried = false;
  pend.placed_mono = 100.0;
  EXPECT_TRUE(ShouldNetSplitBookGuardStaleReprice(pend, "SUBMITTED", 0.0, 100.0, 112.5, 12.0,
                                                  true));
}

TEST(NetSplitLegWait, BookGuardStaleNotReadyBeforeTimeout) {
  NetSplitPendingSnapshot pend;
  pend.book_guard_emergency = true;
  pend.book_guard_emergency_limit = true;
  pend.placed_mono = 100.0;
  EXPECT_FALSE(ShouldNetSplitBookGuardStaleReprice(pend, "SUBMITTED", 0.0, 100.0, 110.0, 12.0,
                                                   true));
}

TEST(NetSplitLegWait, NormalStaleRepriceAfterTimeout) {
  EXPECT_TRUE(ShouldNetSplitStaleReprice("SIM_MARKET", "SUBMITTED", 0.0, 50.0, 100.0, 113.0, 12.0));
}

TEST(NetSplitLegWait, NormalStaleSkipsWhenFilled) {
  EXPECT_FALSE(
      ShouldNetSplitStaleReprice("SIM_MARKET", "SUBMITTED", 50.0, 50.0, 100.0, 113.0, 12.0));
}

TEST(NetSplitLegWait, ResetEntryRetryState) {
  mmtrade::strategy::NetSplitRetryState rt;
  rt.entry_retry_failed_count = 2;
  rt.entry_force_market_next = true;
  mmtrade::strategy::ResetNetSplitLegRetryState(&rt, mmtrade::strategy::NetSplitScene::Entry);
  EXPECT_EQ(rt.entry_retry_failed_count, 0);
  EXPECT_FALSE(rt.entry_force_market_next);
}
