#include "config/execution_util.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace mmtrade {
namespace config {
namespace {

std::string UpperTrim(const std::string &s) {
  std::string out = s;
  out.erase(out.begin(), std::find_if(out.begin(), out.end(),
                                      [](unsigned char c) { return !std::isspace(c); }));
  out.erase(std::find_if(out.rbegin(), out.rend(),
                         [](unsigned char c) { return !std::isspace(c); })
                .base(),
            out.end());
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return out;
}

}  // namespace

std::string NormalizeExecutionOrderType(const std::string &raw) {
  if (raw.empty()) {
    return "NORMAL";
  }
  const std::string t = raw;
  if (t == "市价") {
    return "MARKET";
  }
  if (t == "限价") {
    return "NORMAL";
  }
  const std::string u = UpperTrim(raw);
  if (u == "SIM_MARKET" || u == "SIM-MARKET" || u == "SIMMARKET") {
    return "SIM_MARKET";
  }
  if (u == "NORMAL" || u == "LIMIT") {
    return "NORMAL";
  }
  if (u == "MARKET") {
    return "MARKET";
  }
  throw std::runtime_error("execution.order_type invalid: " + raw);
}

}  // namespace config
}  // namespace mmtrade
