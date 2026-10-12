#pragma once

#include <cstdint>
#include <string>

namespace mmtrade {
namespace gateway {

struct GatewayConfig {
  std::string bind_host;
  int bind_port;
  std::string opend_host;
  int opend_port;
  std::string trd_env;
  std::string trd_market;
  std::uint64_t acc_id;
  int acc_index;
  std::size_t queue_maxsize;
  double moomoo_window_sec;
  int moomoo_max_per_window;
  double moomoo_min_interval_sec;
  int moomoo_reserve_slots;
  double moomoo_freq_backoff_sec;
  double moomoo_pre_place_max_wait_sec;
  double request_timeout_sec;
  bool dry_run;
  std::string log_dir;

  GatewayConfig()
      : bind_host("127.0.0.1"),
        bind_port(18766),
        opend_host("127.0.0.1"),
        opend_port(11121),
        trd_env("SIMULATE"),
        trd_market("US"),
        acc_id(0),
        acc_index(0),
        queue_maxsize(2000),
        moomoo_window_sec(30.0),
        moomoo_max_per_window(15),
        moomoo_min_interval_sec(0.02),
        moomoo_reserve_slots(1),
        moomoo_freq_backoff_sec(1.0),
        moomoo_pre_place_max_wait_sec(5.0),
        request_timeout_sec(15.0),
        dry_run(true),
        log_dir("./logs") {}
};

}  // namespace gateway
}  // namespace mmtrade
