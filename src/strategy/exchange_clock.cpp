#include "strategy/exchange_clock.hpp"

#include <ctime>

namespace mmtrade {
namespace strategy {

namespace {

int ExchangeUtcOffsetSeconds(const std::string &code_full) {
  if (code_full.find("HK.") == 0) {
    return 8 * 3600;
  }
  if (code_full.find("US.") == 0) {
    return -5 * 3600;
  }
  return 0;
}

}  // namespace

engine::LocalDateTime ExchangeLocalFromUnix(const std::string &code_full, double unix_ts) {
  engine::LocalDateTime out;
  const time_t t = static_cast<time_t>(unix_ts) + ExchangeUtcOffsetSeconds(code_full);
  struct tm tm_val;
#if defined(_WIN32)
  gmtime_s(&tm_val, &t);
#else
  gmtime_r(&t, &tm_val);
#endif
  out.year = tm_val.tm_year + 1900;
  out.month = tm_val.tm_mon + 1;
  out.day = tm_val.tm_mday;
  out.hour = tm_val.tm_hour;
  out.minute = tm_val.tm_min;
  out.second = tm_val.tm_sec;
  out.weekday = (tm_val.tm_wday + 6) % 7;
  return out;
}

}  // namespace strategy
}  // namespace mmtrade
