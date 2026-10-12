#pragma once

#include "engine/condition_types.hpp"
#include "engine/session_close_util.hpp"

namespace mmtrade {
namespace strategy {

struct StrategyTickContext {
  double now;
  engine::FieldMap fields;
  double bid_1;
  double ask_1;
  bool has_bid_ask;
  double gain_fraction;
  bool has_gain_fraction;
  engine::LocalDateTime exchange_local_now;
  bool has_exchange_local_now;

  StrategyTickContext()
      : now(0.0),
        bid_1(0.0),
        ask_1(0.0),
        has_bid_ask(false),
        gain_fraction(0.0),
        has_gain_fraction(false),
        has_exchange_local_now(false) {}
};

struct BookTick {
  double bid_1;
  double ask_1;
  bool valid;

  BookTick() : bid_1(0.0), ask_1(0.0), valid(false) {}
};

}  // namespace strategy
}  // namespace mmtrade
