#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mmtrade {
namespace opend {

struct AccountInfo {
  std::uint64_t acc_id;
  bool simulate;
};

struct FundsInfo {
  double cash;
  double avl_withdrawal_cash;
  double power;
  double total_assets;
  double market_val;
  std::string currency;
};

struct PositionInfo {
  std::string code;
  double qty;
  double can_sell_qty;
  double cost_price;
  double market_val;
  double pl_val;
  std::string position_side;
};

typedef std::vector<AccountInfo> AccountList;
typedef std::vector<PositionInfo> PositionList;

}  // namespace opend
}  // namespace mmtrade
