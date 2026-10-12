#include "strategy/net_split_config.hpp"
#include "strategy/net_split_util.hpp"

#include <gtest/gtest.h>

#include <cstdlib>

using mmtrade::strategy::EntryLeg;
using mmtrade::strategy::EntryNetSplitEnabled;
using mmtrade::strategy::EntryNetSplitMarkets;
using mmtrade::strategy::LegIsHkShortOpen;
using mmtrade::strategy::LegPurpose;
using mmtrade::strategy::LegRefPx;
using mmtrade::strategy::NetSplitLegRemark;

class EnvGuard {
 public:
  explicit EnvGuard(const char *name) : name_(name), had_(std::getenv(name) != NULL) {
    if (had_) {
      saved_ = std::getenv(name_);
    }
  }
  ~EnvGuard() {
    if (had_) {
      setenv(name_, saved_.c_str(), 1);
    } else {
      unsetenv(name_);
    }
  }
  void Set(const char *value) { setenv(name_, value, 1); }
  void Unset() { unsetenv(name_); }

 private:
  const char *name_;
  bool had_;
  std::string saved_;
};

TEST(NetSplitRemark, ExitRemarkFits64Bytes) {
  const std::string base = "nana_paper_US_AMD_178516-x-X1_EXIT_NOW";
  const EntryLeg leg{"SELL", 20, LegPurpose::CloseOpposite};
  const std::string out = NetSplitLegRemark(base, leg, 0, 2);
  EXPECT_LE(out.size(), 64u);
  EXPECT_NE(out.find("|ns:co:1/2"), std::string::npos);
}

TEST(NetSplitRemark, LongBaseTruncatedSuffixPreserved) {
  const std::string base = "nana_paper_US_AMD_1785167428578-x-X1_EXIT_NOW";
  const EntryLeg leg{"SELL", 10, LegPurpose::OpenTarget};
  const std::string out = NetSplitLegRemark(base, leg, 1, 2);
  EXPECT_LE(out.size(), 64u);
  EXPECT_NE(out.find("|ns:ot:2/2"), std::string::npos);
}

TEST(EntryNetSplitGate, CppDisabledByDefault) {
  EnvGuard g("CPP_NET_SPLIT_ENABLED");
  g.Unset();
  EXPECT_FALSE(mmtrade::strategy::CppNetSplitEnabled());
}

TEST(EntryNetSplitGate, CppActiveRequiresBothFlags) {
  EnvGuard cpp("CPP_NET_SPLIT_ENABLED");
  cpp.Set("1");
  EnvGuard entry("ENTRY_NET_SPLIT_ENABLED");
  entry.Set("1");
  EnvGuard m("ENTRY_NET_SPLIT_MARKETS");
  m.Unset();
  EXPECT_TRUE(mmtrade::strategy::CppNetSplitActiveForCode("US.AAPL"));
}

TEST(EntryNetSplitHKUS, DefaultMarketsIncludeHkAndUs) {
  EnvGuard g("ENTRY_NET_SPLIT_MARKETS");
  g.Unset();
  const auto markets = EntryNetSplitMarkets();
  EXPECT_NE(markets.find("US"), markets.end());
  EXPECT_NE(markets.find("HK"), markets.end());
}

TEST(EntryNetSplitHKUS, CloseOppositeSellNotHkShortOpen) {
  const EntryLeg leg{"SELL", 400, LegPurpose::CloseOpposite};
  EXPECT_FALSE(LegIsHkShortOpen("HK.02208", leg, "SELL"));
}

TEST(EntryNetSplitHKUS, OpenTargetSellIsHkShortOpen) {
  const EntryLeg leg{"SELL", 200, LegPurpose::OpenTarget};
  EXPECT_TRUE(LegIsHkShortOpen("HK.02208", leg, "SELL"));
}

TEST(EntryNetSplitHKUS, LegRefPxCloseUsesBid) {
  const EntryLeg leg{"SELL", 400, LegPurpose::CloseOpposite};
  EXPECT_NEAR(LegRefPx(leg, "HK.02208", "SELL", 10.0, 9.99, 10.01), 9.99, 1e-9);
}

TEST(EntryNetSplitHKUS, LegRefPxOpenShortUsesAsk) {
  const EntryLeg leg{"SELL", 200, LegPurpose::OpenTarget};
  EXPECT_NEAR(LegRefPx(leg, "HK.02208", "SELL", 9.99, 9.99, 10.01), 10.01, 1e-9);
}

TEST(EntryNetSplitGate, AppliesWhenEnabledForUsMarket) {
  EnvGuard e("CPP_NET_SPLIT_ENABLED");
  e.Set("1");
  EnvGuard s("ENTRY_NET_SPLIT_ENABLED");
  s.Set("1");
  EnvGuard m("ENTRY_NET_SPLIT_MARKETS");
  m.Unset();
  EXPECT_TRUE(mmtrade::strategy::CppNetSplitActiveForCode("US.AAPL"));
  EXPECT_TRUE(mmtrade::strategy::CppNetSplitActiveForCode("HK.00700"));
}
