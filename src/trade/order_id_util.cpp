#include "trade/order_id_util.hpp"

#include <sstream>

namespace mmtrade {
namespace trade {

std::string FormatOrderId(std::uint64_t order_id) {
  if (order_id == 0) {
    return "";
  }
  std::ostringstream oss;
  oss << order_id;
  return oss.str();
}

std::string ResolveDisplayOrderId(std::uint64_t order_id, const std::string &order_id_ex) {
  if (!order_id_ex.empty()) {
    return order_id_ex;
  }
  return FormatOrderId(order_id);
}

std::uint64_t ParseOrderIdUint64(const std::string &raw) {
  if (raw.empty()) {
    return 0;
  }
  std::uint64_t out = 0;
  for (size_t i = 0; i < raw.size(); ++i) {
    const char c = raw[i];
    if (c < '0' || c > '9') {
      return 0;
    }
    out = out * 10 + static_cast<std::uint64_t>(c - '0');
  }
  return out;
}

std::string ExtractOrderIdString(const std::string &raw) {
  if (raw.empty()) {
    return "";
  }
  // Accept plain numeric order ids from logs/fixtures.
  bool numeric = true;
  for (size_t i = 0; i < raw.size(); ++i) {
    const char c = raw[i];
    if (i == 0 && c == '-') {
      continue;
    }
    if (c < '0' || c > '9') {
      numeric = false;
      break;
    }
  }
  if (numeric) {
    return raw;
  }
  const std::string key = "\"order_id\"";
  const size_t pos = raw.find(key);
  if (pos == std::string::npos) {
    return "";
  }
  size_t start = raw.find(':', pos + key.size());
  if (start == std::string::npos) {
    return "";
  }
  ++start;
  while (start < raw.size() && (raw[start] == ' ' || raw[start] == '\t')) {
    ++start;
  }
  if (start >= raw.size()) {
    return "";
  }
  if (raw[start] == '"') {
    ++start;
    const size_t end = raw.find('"', start);
    if (end == std::string::npos) {
      return "";
    }
    return raw.substr(start, end - start);
  }
  size_t end = start;
  while (end < raw.size() && raw[end] >= '0' && raw[end] <= '9') {
    ++end;
  }
  if (end == start) {
    return "";
  }
  return raw.substr(start, end - start);
}

}  // namespace trade
}  // namespace mmtrade
