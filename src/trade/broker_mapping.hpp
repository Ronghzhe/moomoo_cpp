#pragma once

#include "trade/broker_types.hpp"

#include <cstdint>
#include <string>

namespace mmtrade {
namespace trade {

struct OpenDPlaceSnapshot {
  bool ok;
  int ret_code;
  std::uint64_t order_id;
  std::string order_id_ex;
  std::string error;

  OpenDPlaceSnapshot() : ok(false), ret_code(-1), order_id(0) {}
};

PlaceOrderResult MapPlaceSnapshot(const OpenDPlaceSnapshot &raw);

}  // namespace trade
}  // namespace mmtrade
