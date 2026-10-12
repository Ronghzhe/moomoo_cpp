#pragma once

#include <string>
#include <vector>

namespace mmtrade {
namespace strategy {

enum class LegPurpose { CloseOpposite, OpenTarget };

struct EntryLeg {
  std::string side;
  double qty = 0.0;
  LegPurpose purpose = LegPurpose::OpenTarget;

  EntryLeg() = default;
  EntryLeg(std::string side_in, double qty_in, LegPurpose purpose_in)
      : side(std::move(side_in)), qty(qty_in), purpose(purpose_in) {}
};

const char *LegPurposeToString(LegPurpose purpose);
LegPurpose LegPurposeFromString(const std::string &purpose);

std::vector<EntryLeg> PlanEntryLegs(const std::string &entry_side, double qty,
                                    double broker_long, double broker_short);

std::vector<EntryLeg> PlanExitLegs(const std::string &entry_side, double qty,
                                   double broker_long, double broker_short);

}  // namespace strategy
}  // namespace mmtrade
