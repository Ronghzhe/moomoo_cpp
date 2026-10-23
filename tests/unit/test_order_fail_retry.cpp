#include "config/strategy_types.hpp"
#include "strategy/order_fail_retry.hpp"
#include "strategy/symbol_loop_state.hpp"

#include <gtest/gtest.h>

using mmtrade::config::StrategyConfig;
using mmtrade::strategy::BumpEntryOrderFail;
using mmtrade::strategy::BumpExitOrderFail;
using mmtrade::strategy::OrderFailBumpResult;
using mmtrade::strategy::OrderFailRetryWaitSec;
using mmtrade::strategy::ResolveEntryOrderTypeForRetry;
using mmtrade::strategy::SymbolLoopState;

namespace {

StrategyConfig MakeCfg() {
  StrategyConfig cfg;
  cfg.order_retry_max = 3;
  cfg.order_cancel_retry_wait_sec = 2.0;
  cfg.order_limit_price_retry_wait_sec = 0.5;
  cfg.order_too_fast_retry_wait_sec = 1.0;
  cfg.order_rate_limit_retry_wait_sec = 2.0;
  return cfg;
}

}  // namespace

TEST(OrderFailRetry, WaitSecsByClass) {
  const StrategyConfig cfg = MakeCfg();
  EXPECT_DOUBLE_EQ(OrderFailRetryWaitSec(cfg, "CANCELLED_ALL", ""), 2.0);
  EXPECT_DOUBLE_EQ(OrderFailRetryWaitSec(cfg, "", "Operating too fast"), 1.0);
  EXPECT_DOUBLE_EQ(OrderFailRetryWaitSec(cfg, "", "high frequency Maximum 15 times per 30 seconds"),
                   2.0);
  EXPECT_DOUBLE_EQ(OrderFailRetryWaitSec(cfg, "", "invalid price"), 0.5);
  EXPECT_DOUBLE_EQ(OrderFailRetryWaitSec(cfg, "", "limit price is much higher"), 0.5);
}

TEST(OrderFailRetry, EntryNonRetriableIsFatal) {
  SymbolLoopState st;
  st.sym.code = "US.AAPL";
  st.sym.order_type = "NORMAL";
  st.sym.entry_side = "BUY";
  const StrategyConfig cfg = MakeCfg();
  EXPECT_EQ(BumpEntryOrderFail(&st, cfg, 100.0, "Short selling not supported", "", "FAILED", NULL),
            OrderFailBumpResult::kFatal);
  EXPECT_TRUE(st.entry_blocked);
  EXPECT_EQ(st.trading_fatal_code, "ENTRY_ORDER_FAILED");
}

TEST(OrderFailRetry, EntryRetriableThenMarketThenExceed) {
  SymbolLoopState st;
  st.sym.code = "US.AAPL";
  st.sym.order_type = "SIM_MARKET";
  st.sym.entry_side = "BUY";
  const StrategyConfig cfg = MakeCfg();

  EXPECT_EQ(BumpEntryOrderFail(&st, cfg, 10.0, "Operating too fast", "1", "", NULL),
            OrderFailBumpResult::kContinue);
  EXPECT_EQ(st.entry_retry_failed_count, 1);
  EXPECT_NEAR(st.place_cooldown_until_mono, 11.0, 1e-9);

  EXPECT_EQ(BumpEntryOrderFail(&st, cfg, 20.0, "invalid price", "2", "", NULL),
            OrderFailBumpResult::kContinue);
  EXPECT_EQ(BumpEntryOrderFail(&st, cfg, 30.0, "Please try again later", "3", "", NULL),
            OrderFailBumpResult::kContinue);
  EXPECT_TRUE(st.entry_force_market_next);
  EXPECT_EQ(st.entry_retry_failed_count, 3);

  auto choice = ResolveEntryOrderTypeForRetry(&st, cfg);
  EXPECT_EQ(choice.order_type, "MARKET");
  EXPECT_TRUE(choice.market_fallback);
  EXPECT_FALSE(st.entry_force_market_next);

  EXPECT_EQ(BumpEntryOrderFail(&st, cfg, 40.0, "CANCELLED_ALL", "4", "CANCELLED_ALL", NULL),
            OrderFailBumpResult::kFatal);
  EXPECT_EQ(st.trading_fatal_code, "ENTRY_RETRY_EXCEEDED");
}

TEST(OrderFailRetry, AllSessionPostUsesOvernightFatalCode) {
  SymbolLoopState st;
  st.sym.code = "US.AAPL";
  st.sym.order_type = "SIM_MARKET";
  st.sym.entry_side = "BUY";
  st.sym.order_session = "ALL";
  st.last_market_phase = "post";
  st.entry_retry_failed_count = 3;
  const StrategyConfig cfg = MakeCfg();
  EXPECT_EQ(BumpEntryOrderFail(&st, cfg, 40.0, "CANCELLED_ALL", "4", "CANCELLED_ALL", NULL),
            OrderFailBumpResult::kFatal);
  EXPECT_EQ(st.trading_fatal_code, "ENTRY_RETRY_EXCEEDED_OVERNIGHT");
}

TEST(OrderFailRetry, AllSessionMarketOnlyInRthPhase) {
  SymbolLoopState st;
  st.sym.code = "US.AAPL";
  st.sym.order_type = "SIM_MARKET";
  st.sym.entry_side = "BUY";
  st.sym.order_session = "ALL";
  st.entry_retry_failed_count = 3;
  const StrategyConfig cfg = MakeCfg();

  auto night = ResolveEntryOrderTypeForRetry(&st, cfg);
  EXPECT_NE(night.order_type, "MARKET");

  st.last_market_phase = "rth";
  st.entry_force_market_next = false;
  auto rth = ResolveEntryOrderTypeForRetry(&st, cfg);
  EXPECT_EQ(rth.order_type, "MARKET");
  EXPECT_TRUE(rth.market_fallback);

  st.last_market_phase = "post";
  st.entry_retry_failed_count = 3;
  auto post = ResolveEntryOrderTypeForRetry(&st, cfg);
  EXPECT_NE(post.order_type, "MARKET");
}

TEST(OrderFailRetry, ExitNonRetriableIsFatal) {
  SymbolLoopState st;
  st.sym.code = "US.AAPL";
  const StrategyConfig cfg = MakeCfg();
  EXPECT_EQ(BumpExitOrderFail(&st, cfg, 1.0, "account frozen", "", "FAILED", NULL),
            OrderFailBumpResult::kFatal);
  EXPECT_EQ(st.trading_fatal_code, "EXIT_ORDER_FAILED");
}
