#include "strategy/book_guard.hpp"

#include "market/hk_tick.hpp"
#include "strategy/hk_short_params.hpp"
#include "strategy/trade_side_util.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace mmtrade {
namespace strategy {

namespace {

std::string Upper(const std::string &s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return out;
}

bool EnvEnabled(const char *name, bool default_on) {
  const char *raw = std::getenv(name);
  if (raw == NULL || raw[0] == '\0') {
    return default_on;
  }
  const std::string v = Upper(raw);
  return v == "1" || v == "TRUE" || v == "YES";
}

}  // namespace

bool IsValidOrderbookPair(double bid_1, double ask_1) {
  return bid_1 > 0.0 && ask_1 > 0.0 && bid_1 < ask_1;
}

bool BookGuardShouldSkip(double bid_1, double ask_1) {
  return !IsValidOrderbookPair(bid_1, ask_1);
}

bool BookGuardEmergencyEnabled() {
  return EnvEnabled("BOOK_GUARD_EMERGENCY_EXIT_ENABLE", true);
}

double BookGuardEmergencyStaleSec() {
  const char *raw = std::getenv("BOOK_GUARD_EMERGENCY_EXIT_STALE_SEC");
  if (raw != NULL && raw[0] != '\0') {
    const double v = std::atof(raw);
    if (v >= 0.0) {
      return v;
    }
  }
  return 12.0;
}

void ResetBookGuardEmergencyState(BookGuardRuntime *rt) {
  if (rt == NULL) {
    return;
  }
  rt->entry_book_guard_emergency_started = false;
  rt->exit_book_guard_emergency_started = false;
  rt->entry_book_guard_emergency_market_tried = false;
  rt->exit_book_guard_emergency_market_tried = false;
}

RefPriceResult ResolveRefPriceWithBookGuard(const std::string &side, double bid_1, double ask_1,
                                            BookGuardRuntime *rt, double now_ts, bool short_open) {
  RefPriceResult out;
  if (rt == NULL) {
    return out;
  }
  const std::string side_u = Upper(NormalizeTradeSide(side));
  if (IsValidOrderbookPair(bid_1, ask_1)) {
    ResetBookGuardEmergencyState(rt);
    rt->last_valid_bid_1 = bid_1;
    rt->last_valid_ask_1 = ask_1;
    out.ok = true;
    out.price = (side_u == "BUY" || short_open) ? ask_1 : bid_1;
    return out;
  }

  if (now_ts - rt->last_bad_book_log_ts >= 15.0) {
    rt->last_bad_book_log_ts = now_ts;
  }

  if (IsValidOrderbookPair(rt->last_valid_bid_1, rt->last_valid_ask_1)) {
    if (now_ts - rt->last_book_fallback_log_ts >= 15.0) {
      rt->last_book_fallback_log_ts = now_ts;
    }
    out.ok = true;
    out.used_fallback = true;
    out.price = (side_u == "BUY" || short_open) ? rt->last_valid_ask_1 : rt->last_valid_bid_1;
    return out;
  }

  if (now_ts - rt->last_book_skip_log_ts >= 15.0) {
    rt->last_book_skip_log_ts = now_ts;
  }
  return out;
}

EmergencyRefResult EmergencyRefPrice(const std::string &side, double bid_1, double ask_1,
                                     double opened_price, double last_price) {
  EmergencyRefResult out;
  if (last_price > 0.0) {
    out.price = last_price;
    out.source = "last_price";
    return out;
  }
  const std::string side_u = Upper(NormalizeTradeSide(side));
  if (bid_1 > 0.0 && ask_1 > 0.0) {
    out.price = side_u == "SELL" ? std::min(bid_1, ask_1) : std::max(bid_1, ask_1);
    out.source = side_u == "SELL" ? "min_bid_ask" : "max_bid_ask";
    return out;
  }
  if (opened_price > 0.0) {
    out.price = opened_price;
    out.source = "opened_price";
  }
  return out;
}

double EmergencyLimitSubmitPx(const std::string &code, const std::string &side, double ref_px) {
  const std::string s = Upper(NormalizeTradeSide(side));
  double px = ref_px;
  if (IsHkCode(code) && px > 0.0) {
    px = mmtrade::market::AlignPriceToHkTick(px, s.c_str(), ref_px, NULL);
  }
  return px;
}

}  // namespace strategy
}  // namespace mmtrade
