#pragma once

#include "strategy/entry_net_split.hpp"

namespace mmtrade {
namespace strategy {

struct NetSplitLegFillResult {
  bool ok;
  double fill_qty;
  double fill_px;
  std::string fill_status;
  std::string oid;
  std::string error;
};

NetSplitLegFillResult ExecuteNetSplitLegSync(const EntryNetSplitParams &ctx, NetSplitScene scene,
                                             const EntryLeg &leg, int leg_idx, int leg_total,
                                             const std::string &entry_side_for_leg,
                                             bool book_guard_emergency,
                                             const std::string &exit_rule_id = "");

}  // namespace strategy
}  // namespace mmtrade
