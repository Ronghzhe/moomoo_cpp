#pragma once

#include "strategy/entry_net_planner.hpp"
#include "strategy/net_split_remark.hpp"

#include <string>
#include <vector>

namespace mmtrade {
namespace strategy {

std::string NetSplitLegRemark(const std::string &base_remark, const EntryLeg &leg, int idx,
                              int total);

bool LegIsHkShortOpen(const std::string &trade_code, const EntryLeg &leg,
                      const std::string &entry_side);

double LegRefPx(const EntryLeg &leg, const std::string &trade_code,
                const std::string &entry_side, double px, double bid_1, double ask_1);

std::string ExitEntrySideForLeg(const std::string &sym_entry_side, LegPurpose leg_purpose);

bool NeedsNetSplit(const std::vector<EntryLeg> &legs);

}  // namespace strategy
}  // namespace mmtrade
