#include "strategy/entry_net_planner.hpp"
#include "strategy/hk_short_params.hpp"
#include "strategy/net_split_rollback.hpp"

#include <gtest/gtest.h>

using mmtrade::strategy::AggregateLegFillsVwap;
using mmtrade::strategy::EntryLeg;
using mmtrade::strategy::FormatNetSplitFailDetail;
using mmtrade::strategy::LegPurpose;
using mmtrade::strategy::NetSplitLegFill;
using mmtrade::strategy::NetSplitScene;
using mmtrade::strategy::NetSplitSession;
using mmtrade::strategy::RollbackGapOrders;

TEST(NetSplitRollback, RollbackGapBuyBackLong) {
  const auto gaps = RollbackGapOrders(5200, 0, 4800, 0);
  ASSERT_EQ(gaps.size(), 1u);
  EXPECT_EQ(gaps[0].first, "BUY");
  EXPECT_DOUBLE_EQ(gaps[0].second, 400);
}

TEST(NetSplitRollback, RollbackGapCoverShort) {
  const auto gaps = RollbackGapOrders(0, 0, 0, 200);
  ASSERT_EQ(gaps.size(), 1u);
  EXPECT_EQ(gaps[0].first, "BUY");
  EXPECT_DOUBLE_EQ(gaps[0].second, 200);
}

TEST(NetSplitRollback, AggregateVwap) {
  const std::vector<NetSplitLegFill> fills = {
      {0, "close_opposite", "SELL", 400, 24.0, "a"},
      {1, "open_target", "SELL", 200, 24.5, "b"},
  };
  const auto [qty, px] = AggregateLegFillsVwap(fills);
  EXPECT_DOUBLE_EQ(qty, 600);
  EXPECT_NEAR(px, (400 * 24 + 200 * 24.5) / 600, 1e-9);
}

TEST(NetSplitRollback, FailDetailFormat) {
  NetSplitSession session;
  session.scene = NetSplitScene::Entry;
  session.trade_code = "HK.00763";
  session.cfg_path = "/tmp/x.json";
  session.leg_total = 2;
  session.pre_broker_long = 5200;
  session.pre_broker_short = 0;
  session.filled_legs.push_back(
      {0, "close_opposite", "SELL", 400, 24.0, "oid1"});

  const EntryLeg failed_leg{"SELL", 200, LegPurpose::OpenTarget};
  const std::string detail = FormatNetSplitFailDetail(
      NetSplitScene::Entry, "HK.00763", 1, failed_leg, "hk_short_entry_unfilled",
      session, true, "oid_rb", 5200, 0);

  EXPECT_NE(detail.find("NET_SPLIT_ENTRY_LEG2_FAIL"), std::string::npos);
  EXPECT_NE(detail.find("rollback_ok"), std::string::npos);
  EXPECT_NE(detail.find("leg1=close_opposite"), std::string::npos);
}

TEST(NetSplitRollback, HkSplitAttemptConstants) {
  EXPECT_EQ(mmtrade::strategy::kHkShortMaxPlaceAttempts, 5);
  EXPECT_EQ(mmtrade::strategy::kHkNetSplitShortMaxPlaceAttempts, 10);
}
