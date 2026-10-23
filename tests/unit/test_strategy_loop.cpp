#include "strategy/fill_detector.hpp"
#include "strategy/order_executor.hpp"
#include "strategy/symbol_loop_state.hpp"

#include <gtest/gtest.h>

TEST(SymbolLoopState, IsAwaitingFillWithOrderIdExOnly) {
  mmtrade::strategy::SymbolLoopState st;
  st.pending.kind = mmtrade::strategy::PendingKind::kEntry;
  st.pending.order_id = 0;
  st.pending.order_id_ex = "FS1D46FA09217A1000";
  EXPECT_TRUE(mmtrade::strategy::IsAwaitingFill(st));
}

TEST(SymbolLoopState, IsAwaitingFillIdleWhenNoIds) {
  mmtrade::strategy::SymbolLoopState st;
  st.pending.kind = mmtrade::strategy::PendingKind::kEntry;
  st.pending.order_id = 0;
  EXPECT_FALSE(mmtrade::strategy::IsAwaitingFill(st));
}

TEST(FillDetector, ResolveOrderIdExOnlyViaPollCache) {
  mmtrade::trade::OrderPushCache cache;
  mmtrade::trade::OrderPushRow row;
  row.order_id = "FS1D46FA09217A1000";
  row.order_status = "FILL";
  row.dealt_qty = 1.0;
  row.dealt_avg_price = 341.96;
  row.numeric_order_id = 123456789;
  row.code = "US.AAPL";
  cache.Ingest(row);
  const double placed_mono = cache.LastUpdateMono("FS1D46FA09217A1000") - 0.5;
  const double now_mono = placed_mono + 2.0;
  double last_poll = 0.0;
  const mmtrade::strategy::FillResolveResult out = mmtrade::strategy::ResolveOrderRowForFill(
      &cache, "FS1D46FA09217A1000", placed_mono, now_mono, 0.0, 0.0, &last_poll, nullptr);
  EXPECT_TRUE(out.has_row);
  EXPECT_EQ(out.source, "push");
  EXPECT_DOUBLE_EQ(out.row.dealt_qty, 1.0);
}

TEST(FillDetector, FilledAllStatus) {
  mmtrade::trade::OrderPushRow row;
  row.order_status = "11";
  row.dealt_qty = 100.0;
  const mmtrade::strategy::FillClassification out =
      mmtrade::strategy::ClassifyOrderFill(row, 100.0);
  EXPECT_EQ(out.outcome, mmtrade::strategy::FillOutcome::kFilled);
}

TEST(FillDetector, ResolveIgnoresStalePushBeforePlacedMono) {
  mmtrade::trade::OrderPushCache cache;
  mmtrade::trade::OrderPushRow stale;
  stale.order_id = "3290876";
  stale.order_status = "15";
  stale.numeric_order_id = 3290876;
  stale.code = "00700";
  cache.Ingest(stale);
  const double placed_mono = cache.LastUpdateMono("3290876") + 1.0;
  const double now_mono = placed_mono + 2.0;
  double last_poll = 0.0;
  const mmtrade::strategy::FillResolveResult out = mmtrade::strategy::ResolveOrderRowForFill(
      &cache, "3290876", placed_mono, now_mono, 0.0, 0.0, &last_poll, nullptr);
  EXPECT_FALSE(out.has_row);
  EXPECT_EQ(out.source, "none");
}

TEST(FillDetector, WaitWhenPartial) {
  mmtrade::trade::OrderPushRow row;
  row.order_status = "10";
  row.dealt_qty = 10.0;
  const mmtrade::strategy::FillClassification out =
      mmtrade::strategy::ClassifyOrderFill(row, 100.0);
  EXPECT_EQ(out.outcome, mmtrade::strategy::FillOutcome::kWait);
}

TEST(OrderExecutor, SimMarketBuyUsesAsk) {
  mmtrade::config::SymbolConfig sym;
  sym.code = "US.AAPL";
  sym.order_type = "SIM_MARKET";
  sym.sim_market_pct = 0.01;
  mmtrade::strategy::BookTick book;
  book.valid = true;
  book.ask_1 = 100.0;
  book.bid_1 = 99.0;
  const double px = mmtrade::strategy::ComputeSubmitPrice(sym, book, "BUY");
  // Python US SIM_MARKET 1st attempt uses 2% sequence, not config sim_market_pct.
  EXPECT_DOUBLE_EQ(px, 102.0);
}

TEST(OrderExecutor, SimMarketSellAlignsUsTick) {
  mmtrade::config::SymbolConfig sym;
  sym.code = "US.COHR";
  sym.order_type = "SIM_MARKET";
  mmtrade::strategy::BookTick book;
  book.valid = true;
  book.bid_1 = 281.13;
  book.ask_1 = 281.5;
  const double px = mmtrade::strategy::ComputeSubmitPrice(sym, book, "SELL");
  // 281.13 * (1 - 0.02) = 275.5074 -> floor to $0.01 tick -> 275.50
  EXPECT_DOUBLE_EQ(px, 275.50);
}

TEST(SymbolLoopState, EntryDoneWhenStatusAtLeastTwo) {
  mmtrade::strategy::SymbolLoopState st;
  st.sym.status = 2;
  st.entry_done = st.sym.status >= 2;
  EXPECT_TRUE(st.entry_done);
  EXPECT_EQ(mmtrade::strategy::StatusAfterEntryFill(100.0, 100.0), 2);
}

TEST(SymbolLoopState, ExitCompleteUsesExitQty) {
  mmtrade::config::SymbolConfig sym;
  sym.opened_qty = 1000.0;
  sym.closed_qty = 1000.0;
  sym.exit_qty = 1000.0;
  EXPECT_TRUE(mmtrade::strategy::ExitCycleComplete(sym));
}

TEST(SymbolLoopState, SymbolManagedDoneAfterExit) {
  mmtrade::strategy::SymbolLoopState st;
  st.sym.exit.enabled = true;
  st.exit_done = true;
  EXPECT_TRUE(mmtrade::strategy::SymbolManagedDone(st));
}

TEST(SymbolLoopState, SymbolManagedDoneEntryOnlyNoAutoExit) {
  mmtrade::strategy::SymbolLoopState st;
  st.sym.exit.enabled = false;
  st.entry_done = true;
  st.exit_done = false;
  EXPECT_TRUE(mmtrade::strategy::SymbolManagedDone(st));
}

TEST(SymbolLoopState, SymbolManagedDoneStillActive) {
  mmtrade::strategy::SymbolLoopState st;
  st.sym.exit.enabled = true;
  st.entry_done = true;
  st.exit_done = false;
  st.sym.status = 2;
  EXPECT_FALSE(mmtrade::strategy::SymbolManagedDone(st));
}
