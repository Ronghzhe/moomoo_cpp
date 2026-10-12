#include "opend/account_config.hpp"
#include "quote/kline_config.hpp"

#include <gtest/gtest.h>

using mmtrade::opend::AccountConfig;
using mmtrade::quote::KlineConfig;

TEST(AccountConfig, DefaultsMatchPython) {
  AccountConfig cfg;
  EXPECT_EQ(cfg.host, "127.0.0.1");
  EXPECT_EQ(cfg.port, 11121);
  EXPECT_EQ(cfg.trd_market, mmtrade::trade::TrdMarket::kUS);
  EXPECT_TRUE(cfg.simulate);
  EXPECT_TRUE(cfg.trd_env_auto);
  EXPECT_EQ(cfg.acc_index, 0);
  EXPECT_EQ(cfg.acc_id, 0u);
  EXPECT_FALSE(cfg.refresh_cache);
}

TEST(KlineConfig, DefaultsMatchPython) {
  KlineConfig cfg;
  EXPECT_EQ(cfg.host, "127.0.0.1");
  EXPECT_EQ(cfg.port, 11121);
  EXPECT_EQ(cfg.code, "US.AAPL");
  EXPECT_EQ(cfg.ktype, "K_DAY");
  EXPECT_EQ(cfg.autype, "QFQ");
  EXPECT_EQ(cfg.count, 30);
  EXPECT_FALSE(cfg.skip_subscribe);
}
