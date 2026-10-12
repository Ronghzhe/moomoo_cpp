#pragma once

#include <string>

namespace mmtrade {
namespace strategy {

std::string NormalizeTradeSide(const std::string &side);
std::string PositionDirectionFromExitSide(const std::string &exit_side);

}  // namespace strategy
}  // namespace mmtrade
