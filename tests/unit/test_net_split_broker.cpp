#include "strategy/net_split_broker.hpp"
#include "strategy/net_split_config.hpp"
#include "strategy/net_split_util.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>
#include <vector>

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

using mmtrade::opend::PositionInfo;
using mmtrade::strategy::BrokerRowSideAndQty;

TEST(NetSplitBroker, HkShortNegativeQty) {
  PositionInfo row;
  row.code = "HK.01772";
  row.qty = -400.0;
  row.position_side = "SHORT";
  const auto side_qty = BrokerRowSideAndQty(row);
  EXPECT_EQ(side_qty.first, "short");
  EXPECT_DOUBLE_EQ(side_qty.second, 400.0);
}

TEST(NetSplitBroker, LongPositiveQty) {
  PositionInfo row;
  row.code = "US.AAPL";
  row.qty = 10.0;
  row.position_side = "LONG";
  const auto side_qty = BrokerRowSideAndQty(row);
  EXPECT_EQ(side_qty.first, "long");
  EXPECT_DOUBLE_EQ(side_qty.second, 10.0);
}

TEST(NetSplitUtil, NeedsNetSplitMultiLeg) {
  using mmtrade::strategy::EntryLeg;
  using mmtrade::strategy::LegPurpose;
  using mmtrade::strategy::NeedsNetSplit;
  const std::vector<EntryLeg> legs = {
      {"SELL", 5, LegPurpose::CloseOpposite},
      {"SELL", 5, LegPurpose::OpenTarget},
  };
  EXPECT_TRUE(NeedsNetSplit(legs));
}

TEST(NetSplitUtil, NeedsNetSplitSingleCloseOpposite) {
  using mmtrade::strategy::EntryLeg;
  using mmtrade::strategy::LegPurpose;
  using mmtrade::strategy::NeedsNetSplit;
  const std::vector<EntryLeg> legs = {{"SELL", 5, LegPurpose::CloseOpposite}};
  EXPECT_TRUE(NeedsNetSplit(legs));
}

TEST(NetSplitConfig, CppNetSplitDisabledByDefault) {
  EnvGuard g("CPP_NET_SPLIT_ENABLED");
  g.Unset();
  EXPECT_FALSE(mmtrade::strategy::CppNetSplitEnabled());
}
