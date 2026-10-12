#pragma once

#include <string>

namespace mmtrade {
namespace strategy {

struct BookGuardRuntime {
  double last_valid_bid_1;
  double last_valid_ask_1;
  double last_bad_book_log_ts;
  double last_book_fallback_log_ts;
  double last_book_skip_log_ts;

  bool entry_book_guard_emergency_started;
  bool exit_book_guard_emergency_started;
  bool entry_book_guard_emergency_market_tried;
  bool exit_book_guard_emergency_market_tried;

  BookGuardRuntime()
      : last_valid_bid_1(0.0),
        last_valid_ask_1(0.0),
        last_bad_book_log_ts(0.0),
        last_book_fallback_log_ts(0.0),
        last_book_skip_log_ts(0.0),
        entry_book_guard_emergency_started(false),
        exit_book_guard_emergency_started(false),
        entry_book_guard_emergency_market_tried(false),
        exit_book_guard_emergency_market_tried(false) {}
};

bool IsValidOrderbookPair(double bid_1, double ask_1);

bool BookGuardShouldSkip(double bid_1, double ask_1);

bool BookGuardEmergencyEnabled();

double BookGuardEmergencyStaleSec();

void ResetBookGuardEmergencyState(BookGuardRuntime *rt);

struct RefPriceResult {
  bool ok;
  double price;
  bool used_fallback;

  RefPriceResult() : ok(false), price(0.0), used_fallback(false) {}
};

RefPriceResult ResolveRefPriceWithBookGuard(const std::string &side, double bid_1, double ask_1,
                                            BookGuardRuntime *rt, double now_ts, bool short_open);

struct EmergencyRefResult {
  double price;
  std::string source;

  EmergencyRefResult() : price(0.0) {}
};

EmergencyRefResult EmergencyRefPrice(const std::string &side, double bid_1, double ask_1,
                                     double opened_price, double last_price);

double EmergencyLimitSubmitPx(const std::string &code, const std::string &side, double ref_px);

}  // namespace strategy
}  // namespace mmtrade
