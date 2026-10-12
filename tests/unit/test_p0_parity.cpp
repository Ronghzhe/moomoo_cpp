#include "strategy/book_guard.hpp"
#include "strategy/hk_short_params.hpp"
#include "strategy/position_sizing.hpp"
#include "strategy/symbol_tradability.hpp"

#include <gtest/gtest.h>

TEST(BookGuardTest, SkipInvertedBook) {
  EXPECT_TRUE(mmtrade::strategy::BookGuardShouldSkip(10.5, 10.0));
  EXPECT_FALSE(mmtrade::strategy::BookGuardShouldSkip(10.0, 10.5));
}

TEST(BookGuardTest, ResolveRefPriceFallback) {
  mmtrade::strategy::BookGuardRuntime rt;
  rt.last_valid_bid_1 = 10.0;
  rt.last_valid_ask_1 = 10.2;
  const mmtrade::strategy::RefPriceResult r =
      mmtrade::strategy::ResolveRefPriceWithBookGuard("BUY", 10.5, 10.0, &rt, 100.0, false);
  EXPECT_TRUE(r.ok);
  EXPECT_TRUE(r.used_fallback);
  EXPECT_DOUBLE_EQ(r.price, 10.2);
}

TEST(BookGuardTest, EmergencyRefUsesLastPrice) {
  const mmtrade::strategy::EmergencyRefResult r =
      mmtrade::strategy::EmergencyRefPrice("SELL", 0.0, 0.0, 0.0, 9.5);
  EXPECT_DOUBLE_EQ(r.price, 9.5);
  EXPECT_EQ(r.source, "last_price");
}

TEST(HkShortTest, BidInAskBand) {
  EXPECT_TRUE(mmtrade::strategy::HkShortBidInAskBand(99.5, 100.0));
  EXPECT_FALSE(mmtrade::strategy::HkShortBidInAskBand(98.0, 100.0));
}

TEST(HkShortTest, ShortOpenEntryDetect) {
  EXPECT_TRUE(mmtrade::strategy::IsHkShortOpenEntry("HK.00700", "SELL", "SELL"));
  EXPECT_FALSE(mmtrade::strategy::IsHkShortOpenEntry("US.AAPL", "SELL", "SELL"));
}

TEST(SymbolTradabilityTest, SuspendedNotTradable) {
  const mmtrade::strategy::TradabilityAssessResult r =
      mmtrade::strategy::AssessSymbolTradability(1, true, 10.0);
  EXPECT_FALSE(r.tradable);
}

TEST(SymbolTradabilityTest, WaitableFailDetail) {
  EXPECT_TRUE(mmtrade::strategy::IsWaitableSymbolNotTradableFail("stock is not tradable now"));
  EXPECT_FALSE(mmtrade::strategy::IsWaitableSymbolNotTradableFail("insufficient buying power"));
}

TEST(PositionSizingTest, BudgetWholeLot) {
  const mmtrade::strategy::EntrySizingResult r =
      mmtrade::strategy::SizeQtyFromBudget(1000.0, 50.0, 1);
  EXPECT_TRUE(r.ok);
  EXPECT_DOUBLE_EQ(r.qty, 20.0);
}

TEST(PositionSizingTest, EntryQtyRemainPartial) {
  mmtrade::config::SymbolConfig sym;
  sym.entry_qty = 100.0;
  sym.opened_qty = 40.0;
  EXPECT_DOUBLE_EQ(mmtrade::strategy::EntryQtyRemain(sym), 60.0);
  EXPECT_EQ(mmtrade::strategy::StatusAfterEntryFillFull(sym, 40.0, 10.0, 30.0, 10.5, 1), 1);
  EXPECT_EQ(mmtrade::strategy::StatusAfterEntryFillFull(sym, 70.0, 10.0, 30.0, 10.5, 1), 2);
}
