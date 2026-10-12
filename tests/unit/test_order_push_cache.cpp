#include "trade/order_push_cache.hpp"

#include <gtest/gtest.h>

#include <thread>

using mmtrade::trade::OrderPushCache;
using mmtrade::trade::OrderPushRow;

namespace {

OrderPushRow FakeRow(const char *order_id, const char *status, double dealt = 0.0) {
  OrderPushRow row;
  row.order_id = order_id;
  row.order_status = status;
  row.dealt_qty = dealt;
  row.dealt_avg_price = 10.5;
  return row;
}

}  // namespace

// Mirrors strategy/tests/test_order_push_hub.py
TEST(OrderPushCache, IngestAndGetRow) {
  OrderPushCache hub;
  hub.Ingest(FakeRow("123", "SUBMITTED"));
  OrderPushRow got;
  ASSERT_TRUE(hub.GetRow("123", &got));
  EXPECT_EQ(got.order_status, "SUBMITTED");
  EXPECT_EQ(hub.GetVersion("123"), 1u);
}

TEST(OrderPushCache, VersionIncrements) {
  OrderPushCache hub;
  hub.Ingest(FakeRow("1", "SUBMITTING"));
  hub.Ingest(FakeRow("1", "SUBMITTED"));
  hub.Ingest(FakeRow("1", "FILLED_ALL", 100.0));
  EXPECT_EQ(hub.GetVersion("1"), 3u);
  OrderPushRow got;
  ASSERT_TRUE(hub.GetRow("1", &got));
  EXPECT_DOUBLE_EQ(got.dealt_qty, 100.0);
}

TEST(OrderPushCache, EraseRemovesRow) {
  OrderPushCache hub;
  hub.Ingest(FakeRow("123", "SUBMITTED"));
  hub.Erase("123");
  OrderPushRow got;
  EXPECT_FALSE(hub.GetRow("123", &got));
  EXPECT_EQ(hub.GetVersion("123"), 0u);
}

TEST(OrderPushCache, IngestKeepsEarlierCreateTimestamp) {
  OrderPushCache hub;
  OrderPushRow created = FakeRow("9", "SUBMITTED");
  created.create_ts_ms = 1000;
  hub.Ingest(created);
  OrderPushRow filled = FakeRow("9", "FILL", 50.0);
  filled.fill_ts_ms = 2500;
  hub.Ingest(filled);
  OrderPushRow got;
  ASSERT_TRUE(hub.GetRow("9", &got));
  EXPECT_EQ(got.create_ts_ms, 1000);
  EXPECT_EQ(got.fill_ts_ms, 2500);
  EXPECT_EQ(got.order_status, "FILL");
}

TEST(OrderPushCache, WaitUpdateReturnsOnNewPush) {
  OrderPushCache hub;
  hub.Ingest(FakeRow("9", "SUBMITTED"));
  const std::uint64_t v0 = hub.GetVersion("9");

  std::thread worker([&hub]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    hub.Ingest(FakeRow("9", "FILLED_ALL", 50.0));
  });

  OrderPushRow row;
  EXPECT_TRUE(hub.WaitUpdate("9", v0, 2.0, &row));
  EXPECT_EQ(row.order_status, "FILLED_ALL");
  worker.join();
}
