#include "strategy/entry_net_planner.hpp"

#include <gtest/gtest.h>

using mmtrade::strategy::EntryLeg;
using mmtrade::strategy::LegPurpose;
using mmtrade::strategy::PlanEntryLegs;
using mmtrade::strategy::PlanExitLegs;

TEST(EntryNetPlanner, OpenShortWithOppositeLongSplits) {
  const auto legs = PlanEntryLegs("SELL", 10, 5, 0);
  ASSERT_EQ(legs.size(), 2u);
  EXPECT_EQ(legs[0].side, "SELL");
  EXPECT_DOUBLE_EQ(legs[0].qty, 5);
  EXPECT_EQ(legs[0].purpose, LegPurpose::CloseOpposite);
  EXPECT_DOUBLE_EQ(legs[1].qty, 5);
  EXPECT_EQ(legs[1].purpose, LegPurpose::OpenTarget);
}

TEST(EntryNetPlanner, OpenShortOnlyReduceLongSingleLeg) {
  const auto legs = PlanEntryLegs("SELL", 5, 10, 0);
  ASSERT_EQ(legs.size(), 1u);
  EXPECT_DOUBLE_EQ(legs[0].qty, 5);
  EXPECT_EQ(legs[0].purpose, LegPurpose::CloseOpposite);
}

TEST(EntryNetPlanner, OpenLongWithOppositeShortSplits) {
  const auto legs = PlanEntryLegs("BUY", 8, 0, 3);
  ASSERT_EQ(legs.size(), 2u);
  EXPECT_EQ(legs[0].purpose, LegPurpose::CloseOpposite);
  EXPECT_DOUBLE_EQ(legs[0].qty, 3);
  EXPECT_DOUBLE_EQ(legs[1].qty, 5);
}

TEST(EntryNetPlanner, OpenShortCleanBookSingleOpen) {
  const auto legs = PlanEntryLegs("SELL", 10, 0, 0);
  ASSERT_EQ(legs.size(), 1u);
  EXPECT_EQ(legs[0].purpose, LegPurpose::OpenTarget);
  EXPECT_DOUBLE_EQ(legs[0].qty, 10);
}

TEST(ExitNetPlanner, CloseLongBrokerNetLongPartial) {
  const auto legs = PlanExitLegs("BUY", 30, 20, 0);
  ASSERT_EQ(legs.size(), 2u);
  EXPECT_EQ(legs[0].side, "SELL");
  EXPECT_DOUBLE_EQ(legs[0].qty, 20);
  EXPECT_EQ(legs[0].purpose, LegPurpose::CloseOpposite);
  EXPECT_EQ(legs[1].side, "SELL");
  EXPECT_DOUBLE_EQ(legs[1].qty, 10);
  EXPECT_EQ(legs[1].purpose, LegPurpose::OpenTarget);
}

TEST(ExitNetPlanner, CloseLongSymmetricToEntryLong) {
  const auto legs = PlanExitLegs("BUY", 30, 20, 0);
  ASSERT_EQ(legs.size(), 2u);
  EXPECT_EQ(legs[0].purpose, LegPurpose::CloseOpposite);
  EXPECT_EQ(legs[0].side, "SELL");
  EXPECT_DOUBLE_EQ(legs[0].qty, 20);
  EXPECT_EQ(legs[1].purpose, LegPurpose::OpenTarget);
  EXPECT_EQ(legs[1].side, "SELL");
  EXPECT_DOUBLE_EQ(legs[1].qty, 10);
}

TEST(ExitNetPlanner, CloseLongFullBrokerMatchSingleLeg) {
  const auto legs = PlanExitLegs("BUY", 10, 10, 0);
  ASSERT_EQ(legs.size(), 1u);
  EXPECT_EQ(legs[0].purpose, LegPurpose::CloseOpposite);
  EXPECT_DOUBLE_EQ(legs[0].qty, 10);
}

TEST(ExitNetPlanner, CloseShortSymmetricToEntryShort) {
  const auto legs = PlanExitLegs("SELL", 10, 0, 3);
  ASSERT_EQ(legs.size(), 2u);
  EXPECT_EQ(legs[0].purpose, LegPurpose::CloseOpposite);
  EXPECT_EQ(legs[0].side, "BUY");
  EXPECT_DOUBLE_EQ(legs[0].qty, 3);
  EXPECT_EQ(legs[1].purpose, LegPurpose::OpenTarget);
  EXPECT_EQ(legs[1].side, "BUY");
  EXPECT_DOUBLE_EQ(legs[1].qty, 7);
}

TEST(ExitNetPlanner, CloseShortOpenLongWhenNoBrokerPosition) {
  const auto legs = PlanExitLegs("SELL", 10, 0, 0);
  ASSERT_EQ(legs.size(), 1u);
  EXPECT_EQ(legs[0].purpose, LegPurpose::OpenTarget);
  EXPECT_EQ(legs[0].side, "BUY");
  EXPECT_DOUBLE_EQ(legs[0].qty, 10);
}
