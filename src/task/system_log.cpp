#include "task/system_log.hpp"

#include "log/ledger_ts.hpp"

#include <fstream>
#include <sstream>

namespace mmtrade {
namespace task {

SystemLogger::SystemLogger(const std::string &path) : path_(path) {}

void SystemLogger::Write(const char *level, const std::string &msg) {
  if (path_.empty()) {
    return;
  }
  std::ostringstream oss;
  oss << mmtrade::log::FormatDatetimeBeijing() << " | " << level << " | task_system_cpp | " << msg
      << "\n";
  std::lock_guard<std::mutex> lk(mu_);
  std::ofstream ofs(path_.c_str(), std::ios::app);
  if (ofs) {
    ofs << oss.str();
  }
}

void SystemLogger::Info(const std::string &msg) { Write("INFO", msg); }
void SystemLogger::Warn(const std::string &msg) { Write("WARN", msg); }
void SystemLogger::Error(const std::string &msg) { Write("ERROR", msg); }

}  // namespace task
}  // namespace mmtrade
