#include "engine/session_close_util.hpp"

namespace mmtrade {
namespace engine {

namespace {

LocalDateTime AddMinutes(const LocalDateTime &base, int delta_minutes) {
  static const int kDaysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  LocalDateTime out = base;
  int total = out.hour * 60 + out.minute + delta_minutes;
  while (total < 0) {
    total += 24 * 60;
    out.day -= 1;
    if (out.day < 1) {
      out.month -= 1;
      if (out.month < 1) {
        out.month = 12;
        out.year -= 1;
      }
      const int dim = kDaysInMonth[out.month - 1];
      out.day = dim;
      out.weekday = (out.weekday + 6) % 7;
    }
  }
  while (total >= 24 * 60) {
    total -= 24 * 60;
    out.day += 1;
    const int dim = kDaysInMonth[out.month - 1];
    if (out.day > dim) {
      out.day = 1;
      out.month += 1;
      if (out.month > 12) {
        out.month = 1;
        out.year += 1;
      }
    }
    out.weekday = (out.weekday + 1) % 7;
  }
  out.hour = total / 60;
  out.minute = total % 60;
  return out;
}

bool LocalLe(const LocalDateTime &a, const LocalDateTime &b) {
  if (a.year != b.year) return a.year < b.year;
  if (a.month != b.month) return a.month < b.month;
  if (a.day != b.day) return a.day < b.day;
  if (a.hour != b.hour) return a.hour < b.hour;
  if (a.minute != b.minute) return a.minute < b.minute;
  return a.second <= b.second;
}

}  // namespace

SessionCloseWindowResult SessionCloseWindowRthLocal(const LocalDateTime &now_local,
                                                    int minutes_before, int close_hour,
                                                    int close_minute) {
  SessionCloseWindowResult out;
  if (now_local.weekday >= 5) {
    out.ok = false;
    out.note = "weekend_skip";
    return out;
  }
  LocalDateTime close_dt = now_local;
  close_dt.hour = close_hour;
  close_dt.minute = close_minute;
  close_dt.second = 0;
  const LocalDateTime win_start = AddMinutes(close_dt, -minutes_before);
  out.ok = LocalLe(win_start, now_local) && LocalLe(now_local, close_dt);
  out.note = out.ok ? "in_window" : "outside_window";
  return out;
}

}  // namespace engine
}  // namespace mmtrade
