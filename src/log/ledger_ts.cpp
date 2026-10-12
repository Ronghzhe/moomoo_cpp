#include "log/ledger_ts.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace mmtrade {
namespace log {

namespace {

std::string FormatLocalFromTime(const std::time_t t, int utc_offset_hours) {
  const std::time_t adj = t + static_cast<std::time_t>(utc_offset_hours * 3600);
  struct tm tm_val;
#if defined(_WIN32)
  gmtime_s(&tm_val, &adj);
#else
  gmtime_r(&adj, &tm_val);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm_val, "%Y-%m-%dT%H:%M:%S");
  return oss.str();
}

}  // namespace

std::string LedgerNowIso(const std::string &symbol) {
  (void)symbol;
  return FormatLocalFromTime(std::time(NULL), 0);
}

std::string RuntimeUpdatedAtUtc() {
  std::string s = FormatLocalFromTime(std::time(NULL), 0);
  return s + "Z";
}

std::string FormatDatetimeBeijing() {
  using Clock = std::chrono::system_clock;
  const Clock::time_point now_tp = Clock::now();
  const std::time_t sec = Clock::to_time_t(now_tp);
  const int ms = static_cast<int>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now_tp.time_since_epoch()).count() %
      1000);

  const std::time_t adj = sec + static_cast<std::time_t>(8 * 3600);
  struct tm tm_val;
#if defined(_WIN32)
  gmtime_s(&tm_val, &adj);
#else
  gmtime_r(&adj, &tm_val);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm_val, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0')
      << ms;
  return oss.str();
}

}  // namespace log
}  // namespace mmtrade
