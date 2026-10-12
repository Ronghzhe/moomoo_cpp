#pragma once

#include <cstdint>

namespace mmtrade {
namespace opend {

struct QuoteSnapshot {
  static const int kMaxDepthLevels = 10;

  double bid_1;
  double ask_1;
  double bid_1_volume;
  double ask_1_volume;
  double bid_prices[kMaxDepthLevels];
  double bid_volumes[kMaxDepthLevels];
  double ask_prices[kMaxDepthLevels];
  double ask_volumes[kMaxDepthLevels];
  int bid_count;
  int ask_count;
  bool valid;
  // OpenD 回调到达本机的时间；交易所时间仅在 OpenD 提供时才有值。
  std::int64_t local_ts_ms;
  std::int64_t exchange_ts_ms;
  double day_open;
  bool has_day_open;
  int lot_size;
  double last_price;
  int sec_status;
  bool suspended;
  bool has_tradability;

  QuoteSnapshot()
      : bid_1(0.0), ask_1(0.0), bid_1_volume(0.0), ask_1_volume(0.0),
        bid_count(0), ask_count(0), valid(false), local_ts_ms(0),
        exchange_ts_ms(0), day_open(0.0),
        has_day_open(false), lot_size(0), last_price(0.0), sec_status(0),
        suspended(false), has_tradability(false) {
    for (int i = 0; i < kMaxDepthLevels; ++i) {
      bid_prices[i] = 0.0;
      bid_volumes[i] = 0.0;
      ask_prices[i] = 0.0;
      ask_volumes[i] = 0.0;
    }
  }
};

}  // namespace opend
}  // namespace mmtrade
