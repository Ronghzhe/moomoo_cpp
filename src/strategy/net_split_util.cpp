#include "strategy/net_split_util.hpp"

#include "strategy/hk_short_params.hpp"
#include "strategy/trade_side_util.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace mmtrade {
namespace strategy {

namespace {

std::string Upper(const std::string &s) { return NormalizeTradeSide(s); }

}  // namespace

std::string NetSplitLegRemark(const std::string &base_remark, const EntryLeg &leg, int idx,
                              int total) {
  const char *purpose_abbr =
      leg.purpose == LegPurpose::CloseOpposite ? "co" : "ot";
  const std::string suffix =
      std::string("|ns:") + purpose_abbr + ":" + std::to_string(idx + 1) + "/" +
      std::to_string(total);
  const int suffix_bytes = static_cast<int>(suffix.size());
  const int budget = std::max(1, 64 - suffix_bytes);
  std::string base = PlaceOrderRemarkUtf8(base_remark, budget);
  std::string out = base + suffix;
  if (static_cast<int>(out.size()) > 64) {
    out = PlaceOrderRemarkUtf8(out, 64);
  }
  return out;
}

bool LegIsHkShortOpen(const std::string &trade_code, const EntryLeg &leg,
                      const std::string &entry_side) {
  if (leg.purpose != LegPurpose::OpenTarget) {
    return false;
  }
  return IsHkShortOpenEntry(trade_code, leg.side, entry_side);
}

double LegRefPx(const EntryLeg &leg, const std::string &trade_code,
                const std::string &entry_side, double px, double bid_1, double ask_1) {
  if (LegIsHkShortOpen(trade_code, leg, entry_side)) {
    if (ask_1 > 0.0) {
      return ask_1;
    }
    return px;
  }
  const std::string side_u = Upper(leg.side);
  if (side_u == "BUY") {
    if (ask_1 > 0.0) {
      return ask_1;
    }
  } else {
    if (bid_1 > 0.0) {
      return bid_1;
    }
  }
  return px;
}

std::string ExitEntrySideForLeg(const std::string &sym_entry_side,
                                LegPurpose leg_purpose) {
  const std::string es = Upper(sym_entry_side);
  if (leg_purpose == LegPurpose::OpenTarget) {
    if (es == "BUY" || es == "LONG") {
      return "SELL";
    }
    if (es == "SELL" || es == "SHORT") {
      return "BUY";
    }
  }
  return sym_entry_side;
}

bool NeedsNetSplit(const std::vector<EntryLeg> &legs) {
  if (legs.empty()) {
    return false;
  }
  if (legs.size() > 1) {
    return true;
  }
  return legs[0].purpose == LegPurpose::CloseOpposite;
}

}  // namespace strategy
}  // namespace mmtrade
