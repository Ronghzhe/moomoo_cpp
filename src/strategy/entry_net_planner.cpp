#include "strategy/entry_net_planner.hpp"

#include "strategy/trade_side_util.hpp"

#include <algorithm>

namespace mmtrade {
namespace strategy {

namespace {

constexpr double kQtyEps = 1e-9;

std::string TaskEntrySide(const std::string &task_side) {
  const std::string s = NormalizeTradeSide(task_side);
  if (s == "BUY" || s == "LONG") {
    return "BUY";
  }
  if (s == "SELL" || s == "SHORT") {
    return "SELL";
  }
  return s;
}

std::string TaskPositionSide(const std::string &task_side) {
  const std::string s = NormalizeTradeSide(task_side);
  if (s == "BUY" || s == "LONG") {
    return "LONG";
  }
  if (s == "SELL" || s == "SHORT") {
    return "SHORT";
  }
  return s;
}

}  // namespace

const char *LegPurposeToString(LegPurpose purpose) {
  switch (purpose) {
    case LegPurpose::CloseOpposite:
      return "close_opposite";
    case LegPurpose::OpenTarget:
      return "open_target";
  }
  return "open_target";
}

LegPurpose LegPurposeFromString(const std::string &purpose) {
  if (purpose == "close_opposite") {
    return LegPurpose::CloseOpposite;
  }
  return LegPurpose::OpenTarget;
}

std::vector<EntryLeg> PlanEntryLegs(const std::string &entry_side, double qty,
                                    double broker_long, double broker_short) {
  std::vector<EntryLeg> legs;
  double q = qty;
  if (q <= kQtyEps) {
    return legs;
  }

  const std::string side = TaskEntrySide(entry_side);
  const double bl = std::max(0.0, broker_long);
  const double bs = std::max(0.0, broker_short);

  if (side == "BUY") {
    if (bs > kQtyEps) {
      const double close_q = std::min(q, bs);
      legs.push_back(EntryLeg{"BUY", close_q, LegPurpose::CloseOpposite});
      q -= close_q;
    }
    if (q > kQtyEps) {
      legs.push_back(EntryLeg{"BUY", q, LegPurpose::OpenTarget});
    }
    return legs;
  }

  if (side == "SELL") {
    if (bl > kQtyEps) {
      const double close_q = std::min(q, bl);
      legs.push_back(EntryLeg{"SELL", close_q, LegPurpose::CloseOpposite});
      q -= close_q;
    }
    if (q > kQtyEps) {
      legs.push_back(EntryLeg{"SELL", q, LegPurpose::OpenTarget});
    }
    return legs;
  }

  legs.push_back(EntryLeg{side, qty, LegPurpose::OpenTarget});
  return legs;
}

std::vector<EntryLeg> PlanExitLegs(const std::string &entry_side, double qty,
                                   double broker_long, double broker_short) {
  const std::string pos = TaskPositionSide(entry_side);
  std::string exit_order_side;
  if (pos == "LONG") {
    exit_order_side = "SELL";
  } else if (pos == "SHORT") {
    exit_order_side = "BUY";
  } else {
    return {};
  }
  return PlanEntryLegs(exit_order_side, qty, broker_long, broker_short);
}

}  // namespace strategy
}  // namespace mmtrade
