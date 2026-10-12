#include "quote/kline_parse.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

using mmtrade::quote::AuTypeKey;
using mmtrade::quote::KlTypeFileTag;
using mmtrade::quote::KlTypeKey;
using mmtrade::quote::MarketBucket;
using mmtrade::quote::ParseAuTypeKey;
using mmtrade::quote::ParseKlTypeKey;
using mmtrade::quote::ParseQuoteSymbol;
using mmtrade::quote::SymbolFileToken;

// Mirrors kline_client.py static helpers.

TEST(KlineParse, ParseKlTypeKeyAll) {
  EXPECT_EQ(ParseKlTypeKey("K_1M"), KlTypeKey::k1M);
  EXPECT_EQ(ParseKlTypeKey("k_5m"), KlTypeKey::k5M);
  EXPECT_EQ(ParseKlTypeKey("K_DAY"), KlTypeKey::kDay);
  EXPECT_EQ(ParseKlTypeKey("K_QUARTER"), KlTypeKey::kQuarter);
}

TEST(KlineParse, ParseKlTypeKeyInvalid) {
  EXPECT_THROW(ParseKlTypeKey("K_2M"), std::invalid_argument);
}

TEST(KlineParse, ParseAuTypeKey) {
  EXPECT_EQ(ParseAuTypeKey("QFQ"), AuTypeKey::kQfq);
  EXPECT_EQ(ParseAuTypeKey("hfq"), AuTypeKey::kHfq);
  EXPECT_EQ(ParseAuTypeKey("NONE"), AuTypeKey::kNone);
}

TEST(KlineParse, KlTypeFileTag) {
  EXPECT_EQ(KlTypeFileTag(KlTypeKey::k1M), "1M");
  EXPECT_EQ(KlTypeFileTag(KlTypeKey::k5M), "5M");
  EXPECT_EQ(KlTypeFileTag(KlTypeKey::kDay), "1D");
  EXPECT_EQ(KlTypeFileTag(KlTypeKey::kMon), "1MO");
}

TEST(KlineParse, ParseQuoteSymbolUsHk) {
  const mmtrade::quote::QuoteSymbol us = ParseQuoteSymbol("US.AAPL");
  EXPECT_EQ(us.prefix, "US");
  EXPECT_EQ(us.code, "AAPL");
  EXPECT_EQ(us.full, "US.AAPL");

  const mmtrade::quote::QuoteSymbol hk = ParseQuoteSymbol("HK.00700");
  EXPECT_EQ(hk.prefix, "HK");
  EXPECT_EQ(hk.code, "00700");
}

TEST(KlineParse, ParseQuoteSymbolBareTicker) {
  const mmtrade::quote::QuoteSymbol sym = ParseQuoteSymbol("AAPL");
  EXPECT_EQ(sym.prefix, "US");
  EXPECT_EQ(sym.code, "AAPL");
  EXPECT_EQ(sym.full, "US.AAPL");
}

TEST(KlineParse, MarketBucket) {
  EXPECT_EQ(MarketBucket("US.AAPL"), "US");
  EXPECT_EQ(MarketBucket("HK.00700"), "HK");
}

TEST(KlineParse, SymbolFileToken) {
  EXPECT_EQ(SymbolFileToken("JPM/PL"), "JPM_PL");
  EXPECT_EQ(SymbolFileToken("  "), "unknown");
}
