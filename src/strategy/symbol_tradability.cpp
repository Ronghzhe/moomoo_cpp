#include "strategy/symbol_tradability.hpp"

#include <cstdlib>
#include <algorithm>
#include <cctype>

namespace mmtrade {
namespace strategy {

namespace {

// Mirrors Qot_Common::SecurityStatus values (avoid protobuf in moomoo_core).
enum SecStatus {
  kSecNormal = 1,
  kSecListing = 2,
  kSecPurchasing = 3,
  kSecSubscribing = 4,
  kSecBeforeDarkOpening = 5,
  kSecDarkTradeEnd = 7,
  kSecToBeOpen = 8,
  kSecSuspended = 9,
  kSecRecoverableCircuitBreaker = 18,
};

std::string Lower(const std::string &s) {
  std::string out = s;
  for (size_t i = 0; i < out.size(); ++i) {
    out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
  }
  return out;
}

bool SecStatusWait(int sec_status) {
  switch (sec_status) {
    case kSecListing:
    case kSecPurchasing:
    case kSecSubscribing:
    case kSecBeforeDarkOpening:
    case kSecDarkTradeEnd:
    case kSecToBeOpen:
    case kSecSuspended:
    case kSecRecoverableCircuitBreaker:
      return true;
    default:
      return false;
  }
}

}  // namespace

double SymbolTradabilityPollSec() {
  const char *raw = std::getenv("STRATEGY_SYMBOL_TRADABILITY_POLL_SEC");
  if (raw != NULL && raw[0] != '\0') {
    const double v = std::atof(raw);
    if (v >= 5.0) {
      return v;
    }
  }
  return 30.0;
}

TradabilityAssessResult AssessSymbolTradability(int sec_status, bool suspended,
                                                double last_price) {
  TradabilityAssessResult out;
  if (sec_status == kSecNormal && !suspended) {
    out.tradable = true;
    return out;
  }
  if (suspended || SecStatusWait(sec_status)) {
    out.tradable = false;
    out.reason = "sec_status=" + std::to_string(sec_status) +
                 " suspension=" + (suspended ? "true" : "false");
    return out;
  }
  if (!suspended && last_price > 0.0) {
    out.tradable = true;
    return out;
  }
  out.tradable = false;
  out.reason = "sec_status=" + std::to_string(sec_status) + " no_last_price suspension=" +
               (suspended ? "true" : "false");
  return out;
}

bool IsWaitableSymbolNotTradableFail(const std::string &detail) {
  const std::string d = Lower(detail);
  if (d.empty()) {
    return false;
  }
  static const char *kNeedles[] = {
      "not tradable",      "not tradeable",   "cannot trade",    "can't trade",
      "not yet tradable",  "不可交易",        "暂不可",          "尚未开盘",
      "未开盘",            "待上市",          "停牌",            "suspended",
      "halt",              "listing",         "security status", "sec_status",
      "security is not",   "stock is not",    "symbol is not",
  };
  for (size_t i = 0; i < sizeof(kNeedles) / sizeof(kNeedles[0]); ++i) {
    if (d.find(kNeedles[i]) != std::string::npos) {
      return true;
    }
  }
  return false;
}

}  // namespace strategy
}  // namespace mmtrade
