#include "strategy/trade_side_util.hpp"

#include <algorithm>
#include <cctype>

namespace mmtrade {
namespace strategy {

std::string NormalizeTradeSide(const std::string &side) {
  std::string s = side;
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return s;
}

std::string PositionDirectionFromExitSide(const std::string &exit_side) {
  const std::string s = NormalizeTradeSide(exit_side);
  return s == "SELL" ? "BUY" : "SELL";
}

}  // namespace strategy
}  // namespace mmtrade
