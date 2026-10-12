#pragma once

#include "gateway/gateway_runtime.hpp"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace mmtrade {
namespace gateway {

class GatewayHttpServer {
 public:
  explicit GatewayHttpServer(GatewayRuntime *runtime);
  ~GatewayHttpServer();

  bool Start();
  void ServeForever();
  void Stop();

 private:
  void AcceptLoop();
  void HandleClient(int fd);

  GatewayRuntime *runtime_;
  int listen_fd_;
  std::atomic<bool> running_;
  std::thread accept_thread_;
};

}  // namespace gateway
}  // namespace mmtrade
