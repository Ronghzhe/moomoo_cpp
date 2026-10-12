#pragma once

#include "trade/order_parse.hpp"

#include <cstdint>
#include <string>

namespace mmtrade {
namespace opend {

// Mirrors Python AccountClientConfig (Phase 1 subset).
struct AccountConfig {
  std::string host;
  int port;
  trade::TrdMarket trd_market;
  bool simulate;
  bool trd_env_auto;
  int acc_index;
  std::uint64_t acc_id;
  bool refresh_cache;

  AccountConfig()
      : host("127.0.0.1"),
        port(11121),
        trd_market(trade::TrdMarket::kUS),
        simulate(true),
        trd_env_auto(true),
        acc_index(0),
        acc_id(0),
        refresh_cache(false) {}
};

}  // namespace opend
}  // namespace mmtrade
