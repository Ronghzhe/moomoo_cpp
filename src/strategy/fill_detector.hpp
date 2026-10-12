#pragma once

#include "trade/order_push_cache.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace mmtrade {
namespace strategy {

enum class FillOutcome {
  kWait,
  kFilled,
  kFailed,
};

struct FillClassification {
  FillOutcome outcome;
  double dealt_qty;
  double dealt_avg_price;

  FillClassification() : outcome(FillOutcome::kWait), dealt_qty(0.0), dealt_avg_price(0.0) {}
};

FillClassification ClassifyOrderFill(const trade::OrderPushRow &row, double target_qty);

bool IsOrderPushSubmitting(const std::string &order_status);
bool IsOrderPushFilled(const std::string &order_status, double dealt_qty, double target_qty);

// push 优先；报单后 push_silence 秒内不 poll，之后每 poll_min 秒 poll 一次（对齐 Python hybrid）。
double OrderFillPushSilenceSec();

struct FillResolveResult {
  bool has_row;
  trade::OrderPushRow row;
  std::string source;

  FillResolveResult() : has_row(false), source("none") {}
};

FillResolveResult ResolveOrderRowForFill(
    const trade::OrderPushCache *cache, const std::string &order_id, double placed_mono,
    double now_mono, double push_silence_sec, double poll_min_sec, double *last_poll_mono,
    const std::function<bool()> &poll_fn);

}  // namespace strategy
}  // namespace mmtrade
