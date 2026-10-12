#include "market/hk_tick.hpp"

#include <gtest/gtest.h>

using mmtrade::market::AlignPriceToHkTick;
using mmtrade::market::Date;
using mmtrade::market::HkTickSize;

namespace {

const Date kAsOf20260723 = {2026, 7, 23};

}  // namespace

// Mirrors strategy/tests/test_hk_tick_utils.py
TEST(HkTick, Phase1Tick20To50) {
  EXPECT_DOUBLE_EQ(HkTickSize(26.84, &kAsOf20260723), 0.02);
  EXPECT_DOUBLE_EQ(HkTickSize(43.02, &kAsOf20260723), 0.02);
}

TEST(HkTick, Phase1Tick10To20) {
  EXPECT_DOUBLE_EQ(HkTickSize(15.0, &kAsOf20260723), 0.01);
}

TEST(HkTick, Align01810ExitSell) {
  const double px = AlignPriceToHkTick(26.84 * (1.0 - 0.01), "SELL", 26.84, &kAsOf20260723);
  EXPECT_NEAR(px, 26.56, 1e-6);
}

TEST(HkTick, Align01024ExitSell) {
  const double px = AlignPriceToHkTick(43.02 * (1.0 - 0.01), "SELL", 43.02, &kAsOf20260723);
  EXPECT_NEAR(px, 42.58, 1e-6);
}

TEST(HkTick, Align02252ExitBuy) {
  const double px = AlignPriceToHkTick(22.12 * (1.0 + 0.01), "BUY", 22.12, &kAsOf20260723);
  EXPECT_NEAR(px, 22.36, 1e-6);
}

TEST(HkTick, NonPositiveRawReturnsRaw) {
  EXPECT_DOUBLE_EQ(AlignPriceToHkTick(0.0, "BUY", 10.0, &kAsOf20260723), 0.0);
}
