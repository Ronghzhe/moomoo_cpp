#include "engine/compare.hpp"

#include <stdexcept>

namespace mmtrade {
namespace engine {

bool CompareFloat(double left, const std::string &op, double right) {
  if (op == "<") return left < right;
  if (op == "<=") return left <= right;
  if (op == ">") return left > right;
  if (op == ">=") return left >= right;
  if (op == "==") return left == right;
  if (op == "!=") return left != right;
  throw std::invalid_argument("unknown compare operator: " + op);
}

}  // namespace engine
}  // namespace mmtrade
