#include "strategy/net_split_broker.hpp"

#include "strategy/trade_side_util.hpp"

#include <algorithm>
#include <cmath>

namespace mmtrade {
namespace strategy {

namespace {

constexpr double kQtyEps = 1e-6;

std::string Upper(const std::string &s) { return NormalizeTradeSide(s); }

}  // namespace

std::pair<std::string, double> BrokerRowSideAndQty(const opend::PositionInfo &row) {
  const std::string ps = Upper(row.position_side);
  const double qty = row.qty;
  const double can_sell = row.can_sell_qty;

  if (ps == "SHORT" || qty < -kQtyEps || can_sell < -kQtyEps) {
    return {"short", std::max(std::abs(qty), std::abs(can_sell))};
  }
  return {"long", std::max(std::max(qty, can_sell), 0.0)};
}

}  // namespace strategy
}  // namespace mmtrade
