#pragma once

#include <string>

namespace mmtrade {
namespace engine {

struct LocalDateTime {
  int year;
  int month;
  int day;
  int hour;
  int minute;
  int second;
  int weekday;  // 0=Mon .. 6=Sun

  LocalDateTime()
      : year(1970), month(1), day(1), hour(0), minute(0), second(0), weekday(0) {}
};

struct SessionCloseWindowResult {
  bool ok;
  std::string note;

  SessionCloseWindowResult() : ok(false) {}
};

SessionCloseWindowResult SessionCloseWindowRthLocal(const LocalDateTime &now_local,
                                                    int minutes_before, int close_hour = 16,
                                                    int close_minute = 0);

}  // namespace engine
}  // namespace mmtrade
