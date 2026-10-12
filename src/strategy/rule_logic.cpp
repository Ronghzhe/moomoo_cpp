#include "strategy/rule_logic.hpp"

namespace mmtrade {
namespace strategy {

bool BoolMerge(const std::vector<bool> &values, const std::string &logic) {
  if (values.empty()) {
    return false;
  }
  if (logic == "all") {
    for (size_t i = 0; i < values.size(); ++i) {
      if (!values[i]) {
        return false;
      }
    }
    return true;
  }
  for (size_t i = 0; i < values.size(); ++i) {
    if (values[i]) {
      return true;
    }
  }
  return false;
}

}  // namespace strategy
}  // namespace mmtrade
