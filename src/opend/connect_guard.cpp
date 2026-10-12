#include "opend/connect_guard.hpp"

#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <sys/file.h>
#include <thread>
#include <unistd.h>

namespace mmtrade {
namespace opend {
namespace {

bool EnvTruthy(const char *name) {
  const char *raw = std::getenv(name);
  if (raw == NULL || raw[0] == '\0') {
    return false;
  }
  const std::string v(raw);
  return v == "1" || v == "true" || v == "yes" || v == "TRUE" || v == "YES";
}

const char *LockPath() {
  const char *override_path = std::getenv("MOOMOO_OPEND_CONNECT_LOCK");
  if (override_path != NULL && override_path[0] != '\0') {
    return override_path;
  }
  return "/tmp/moomoo_opend_connect.lock";
}

}  // namespace

OpenDConnectGuard::OpenDConnectGuard() : fd_(-1), locked_(false) {
  fd_ = ::open(LockPath(), O_CREAT | O_RDWR, 0666);
  if (fd_ < 0) {
    return;
  }
  locked_ = (::flock(fd_, LOCK_EX) == 0);
}

OpenDConnectGuard::~OpenDConnectGuard() {
  if (locked_ && fd_ >= 0) {
    ::flock(fd_, LOCK_UN);
  }
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  locked_ = false;
}

bool SkipOpenDConnectSleep() {
  return EnvTruthy("MOOMOO_SKIP_OPEND_CONNECT_SLEEP") || EnvTruthy("MOOMOO_BENCH_HOT_PATH");
}

void MaybeSleepAfterOpenDConnect() {
  if (SkipOpenDConnectSleep()) {
    return;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

}  // namespace opend
}  // namespace mmtrade
