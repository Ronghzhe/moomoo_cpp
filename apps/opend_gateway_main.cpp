#include "l1_common.hpp"

#include "gateway/gateway_config.hpp"
#include "gateway/gateway_runtime.hpp"
#include "gateway/http_server.hpp"

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

volatile sig_atomic_t g_run = 1;

void OnSignal(int) { g_run = 0; }

void EnsureDir(const std::string &path) {
  if (path.empty()) {
    return;
  }
  ::mkdir(path.c_str(), 0755);
}

mmtrade::gateway::GatewayConfig ParseArgs(int argc, char **argv) {
  mmtrade::gateway::GatewayConfig cfg;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--bind-host" && i + 1 < argc) {
      cfg.bind_host = argv[++i];
    } else if (a == "--bind-port" && i + 1 < argc) {
      cfg.bind_port = std::atoi(argv[++i]);
    } else if (a == "--opend-host" && i + 1 < argc) {
      cfg.opend_host = argv[++i];
    } else if (a == "--opend-port" && i + 1 < argc) {
      cfg.opend_port = std::atoi(argv[++i]);
    } else if (a == "--trd-env" && i + 1 < argc) {
      cfg.trd_env = argv[++i];
    } else if (a == "--trd-market" && i + 1 < argc) {
      cfg.trd_market = argv[++i];
    } else if (a == "--acc-id" && i + 1 < argc) {
      cfg.acc_id = static_cast<std::uint64_t>(std::strtoull(argv[++i], NULL, 10));
    } else if (a == "--acc-index" && i + 1 < argc) {
      cfg.acc_index = std::atoi(argv[++i]);
    } else if (a == "--moomoo-window-sec" && i + 1 < argc) {
      cfg.moomoo_window_sec = std::atof(argv[++i]);
    } else if (a == "--moomoo-max-per-window" && i + 1 < argc) {
      cfg.moomoo_max_per_window = std::atoi(argv[++i]);
    } else if (a == "--moomoo-min-interval-sec" && i + 1 < argc) {
      cfg.moomoo_min_interval_sec = std::atof(argv[++i]);
    } else if (a == "--moomoo-reserve-slots" && i + 1 < argc) {
      cfg.moomoo_reserve_slots = std::atoi(argv[++i]);
    } else if (a == "--moomoo-freq-backoff-sec" && i + 1 < argc) {
      cfg.moomoo_freq_backoff_sec = std::atof(argv[++i]);
    } else if (a == "--log-dir" && i + 1 < argc) {
      cfg.log_dir = argv[++i];
    } else if (a == "--dry-run") {
      cfg.dry_run = true;
    } else if (a == "--live") {
      cfg.dry_run = false;
    } else if (a == "--help" || a == "-h") {
      std::cout << "opend_gateway_cpp — C++ OpenD place gateway (default :18766)\n"
                   "  --bind-host/--bind-port --opend-host/--opend-port\n"
                   "  --dry-run (default) | --live\n"
                   "  --log-dir\n";
      std::exit(0);
    } else {
      std::cerr << "unknown arg: " << a << "\n";
      std::exit(2);
    }
  }
  return cfg;
}

}  // namespace

int main(int argc, char **argv) {
  const mmtrade::gateway::GatewayConfig cfg = ParseArgs(argc, argv);
  EnsureDir(cfg.log_dir);

  std::signal(SIGINT, OnSignal);
  std::signal(SIGTERM, OnSignal);

  l1::SdkInit();

  mmtrade::gateway::GatewayRuntime runtime(cfg);
  mmtrade::gateway::GatewayHttpServer server(&runtime);
  if (!server.Start()) {
    std::cerr << "failed to bind " << cfg.bind_host << ":" << cfg.bind_port << "\n";
    runtime.Close();
    l1::SdkShutdown();
    return 1;
  }

  std::cout << "opend_gateway_cpp start bind=" << cfg.bind_host << ":" << cfg.bind_port
            << " opend=" << cfg.opend_host << ":" << cfg.opend_port << " env=" << cfg.trd_env
            << " market=" << cfg.trd_market << " dry_run=" << (cfg.dry_run ? "1" : "0")
            << " log_dir=" << cfg.log_dir << std::endl;

  while (g_run) {
    ::sleep(1);
  }

  server.Stop();
  runtime.Close();
  l1::SdkShutdown();
  return 0;
}
