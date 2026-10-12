#pragma once

#include <string>

namespace mmtrade {
namespace log {

void LogConsoleInfo(const std::string &message);
void LogConsoleWarn(const std::string &message);
void LogConsoleError(const std::string &message);

std::string StatusLabel(int status);
std::string BoolMark(bool ok);

}  // namespace log
}  // namespace mmtrade
