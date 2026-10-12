#pragma once

#include <mutex>
#include <string>

namespace mmtrade {
namespace task {

class SystemLogger {
 public:
  explicit SystemLogger(const std::string &path);

  void Info(const std::string &msg);
  void Warn(const std::string &msg);
  void Error(const std::string &msg);

 private:
  std::string path_;
  std::mutex mu_;
  void Write(const char *level, const std::string &msg);
};

}  // namespace task
}  // namespace mmtrade
