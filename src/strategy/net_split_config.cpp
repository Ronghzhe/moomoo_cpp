#include "strategy/net_split_config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace mmtrade {
namespace strategy {

namespace {

std::string Upper(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(s[i]))));
  }
  return out;
}

bool EnvTruthy(const char *name) {
  const char *raw = std::getenv(name);
  if (raw == NULL || raw[0] == '\0') {
    return false;
  }
  const std::string v = Upper(raw);
  return v == "1" || v == "TRUE" || v == "YES";
}

}  // namespace

bool EntryNetSplitEnabled() { return EnvTruthy("ENTRY_NET_SPLIT_ENABLED"); }

std::set<std::string> EntryNetSplitMarkets() {
  const char *raw = std::getenv("ENTRY_NET_SPLIT_MARKETS");
  std::string text = raw != NULL ? raw : "US,HK";
  text = Upper(text);
  if (text.empty()) {
    return {"US", "HK"};
  }
  std::set<std::string> out;
  std::stringstream ss(text);
  std::string part;
  while (std::getline(ss, part, ',')) {
    size_t b = 0;
    while (b < part.size() && part[b] == ' ') {
      ++b;
    }
    size_t e = part.size();
    while (e > b && part[e - 1] == ' ') {
      --e;
    }
    if (e > b) {
      out.insert(part.substr(b, e - b));
    }
  }
  if (out.empty()) {
    out.insert("US");
    out.insert("HK");
  }
  return out;
}

std::string TradeCodeMarket(const std::string &trade_code) {
  const size_t dot = trade_code.find('.');
  if (dot == std::string::npos || dot == 0) {
    return "";
  }
  return Upper(trade_code.substr(0, dot));
}

bool EntryNetSplitAppliesToCode(const std::string &trade_code) {
  if (!EntryNetSplitEnabled()) {
    return false;
  }
  const std::string market = TradeCodeMarket(trade_code);
  if (market.empty()) {
    return false;
  }
  const std::set<std::string> markets = EntryNetSplitMarkets();
  return markets.find(market) != markets.end();
}

bool CppNetSplitEnabled() { return EnvTruthy("CPP_NET_SPLIT_ENABLED"); }

bool CppNetSplitActiveForCode(const std::string &trade_code) {
  return CppNetSplitEnabled() && EntryNetSplitAppliesToCode(trade_code);
}

}  // namespace strategy
}  // namespace mmtrade
