#include "strategy/net_split_remark.hpp"

namespace mmtrade {
namespace strategy {

namespace {

bool IsUtf8Continuation(unsigned char c) { return (c & 0xC0) == 0x80; }

std::string TrimCopy(const std::string &s) {
  size_t b = 0;
  while (b < s.size() && (s[b] == ' ' || s[b] == '\t' || s[b] == '\n' || s[b] == '\r')) {
    ++b;
  }
  size_t e = s.size();
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\n' || s[e - 1] == '\r')) {
    --e;
  }
  return s.substr(b, e - b);
}

}  // namespace

std::string PlaceOrderRemarkUtf8(const std::string &remark, int max_bytes) {
  if (max_bytes <= 0) {
    return "";
  }
  const std::string t = TrimCopy(remark);
  if (static_cast<int>(t.size()) <= max_bytes) {
    return t;
  }
  std::string clipped = t.substr(0, static_cast<size_t>(max_bytes));
  while (!clipped.empty()) {
    size_t i = clipped.size();
    while (i > 0 && IsUtf8Continuation(static_cast<unsigned char>(clipped[i - 1]))) {
      --i;
    }
    if (i == 0) {
      return "";
    }
    clipped.resize(i);
    return clipped;
  }
  return "";
}

}  // namespace strategy
}  // namespace mmtrade
