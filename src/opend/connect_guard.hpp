#pragma once

namespace mmtrade {
namespace opend {

// Cross-process exclusive lock around OpenD InitQot/InitTrd.
// Parallel strategy starts contend on OpenD; serializing Connect recovers connect latency.
class OpenDConnectGuard {
 public:
  OpenDConnectGuard();
  ~OpenDConnectGuard();

  OpenDConnectGuard(const OpenDConnectGuard &) = delete;
  OpenDConnectGuard &operator=(const OpenDConnectGuard &) = delete;

 private:
  int fd_;
  bool locked_;
};

// True when MOOMOO_BENCH_HOT_PATH or MOOMOO_SKIP_OPEND_CONNECT_SLEEP is 1/true/yes.
bool SkipOpenDConnectSleep();

// Sleep 200ms after Connect unless skip is enabled.
void MaybeSleepAfterOpenDConnect();

}  // namespace opend
}  // namespace mmtrade
