#include "strategy/net_split_broker_query.hpp"

#include "strategy/net_split_broker.hpp"
#include "strategy/trade_side_util.hpp"

namespace mmtrade {
namespace strategy {

namespace {

bool CodeMatches(const std::string &want, const std::string &got) {
  return NormalizeTradeSide(want) == NormalizeTradeSide(got);
}

}  // namespace

std::pair<double, double> BrokerLongShortQty(opend::OrderClient *client,
                                             const std::string &code) {
  if (client == NULL) {
    return {0.0, 0.0};
  }
  opend::PositionList rows;
  if (!client->GetPositions(code, &rows)) {
    return {0.0, 0.0};
  }
  double long_q = 0.0;
  double short_q = 0.0;
  for (size_t i = 0; i < rows.size(); ++i) {
    if (!CodeMatches(code, rows[i].code)) {
      continue;
    }
    const std::pair<std::string, double> side_qty = BrokerRowSideAndQty(rows[i]);
    if (side_qty.first == "short") {
      short_q += side_qty.second;
    } else {
      long_q += side_qty.second;
    }
  }
  return {long_q, short_q};
}

}  // namespace strategy
}  // namespace mmtrade
