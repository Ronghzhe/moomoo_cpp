#include "log/strategy_console_log.hpp"

#include "log/ledger_ts.hpp"

#include <iostream>

namespace mmtrade {
namespace log {

namespace {

void LogConsole(const char *level, const std::string &message) {
  std::cerr << "[" << FormatDatetimeBeijing() << "] [" << level << "] " << message << std::endl;
}

}  // namespace

void LogConsoleInfo(const std::string &message) { LogConsole("INFO", message); }

void LogConsoleWarn(const std::string &message) { LogConsole("WARN", message); }

void LogConsoleError(const std::string &message) { LogConsole("ERROR", message); }

std::string StatusLabel(int status) {
  switch (status) {
    case 0:
      return "0(待开仓)";
    case 1:
      return "1(部分开仓)";
    case 2:
      return "2(持仓)";
    case 3:
      return "3(已平仓)";
    default:
      return std::to_string(status);
  }
}

std::string BoolMark(bool ok) { return ok ? "Y" : "N"; }

}  // namespace log
}  // namespace mmtrade
