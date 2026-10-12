#include "strategy/fill_detector.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace mmtrade {
namespace strategy {

namespace {

bool IsTerminalFailureStatus(const std::string &st) {
  return st.find("CANCELLED_ALL") != std::string::npos || st.find("FAILED") != std::string::npos ||
         st.find("DISABLED") != std::string::npos || st == "12" || st == "13" || st == "14";
}

bool IsFilledStatus(const std::string &st) {
  return st.find("FILLED_ALL") != std::string::npos || st == "11";
}

bool IsSubmittingStatus(const std::string &st) {
  return st.find("SUBMIT") != std::string::npos;
}

}  // namespace

bool IsOrderPushSubmitting(const std::string &order_status) {
  return IsSubmittingStatus(order_status);
}

bool IsOrderPushFilled(const std::string &order_status, double dealt_qty, double target_qty) {
  if (IsFilledStatus(order_status)) {
    return true;
  }
  return dealt_qty + 1e-9 >= target_qty && dealt_qty > 0.0;
}

FillClassification ClassifyOrderFill(const trade::OrderPushRow &row, double target_qty) {
  FillClassification out;
  out.dealt_qty = row.dealt_qty;
  out.dealt_avg_price = row.dealt_avg_price;

  if (IsTerminalFailureStatus(row.order_status)) {
    out.outcome = FillOutcome::kFailed;
    return out;
  }
  if (IsFilledStatus(row.order_status) || row.dealt_qty + 1e-9 >= target_qty) {
    out.outcome = FillOutcome::kFilled;
    return out;
  }
  if (row.order_status.find("CANCELLED_PART") != std::string::npos && row.dealt_qty > 0.0) {
    out.outcome = FillOutcome::kFilled;
    return out;
  }
  out.outcome = FillOutcome::kWait;
  return out;
}

double OrderFillPushSilenceSec() {
  const char *raw = std::getenv("ORDER_FILL_PUSH_SILENCE_SEC");
  if (raw != NULL && raw[0] != '\0') {
    const double v = std::atof(raw);
    if (v >= 0.0) {
      return std::max(0.0, v);
    }
  }
  return 1.0;
}

FillResolveResult ResolveOrderRowForFill(
    const trade::OrderPushCache *cache, const std::string &order_id, double placed_mono,
    double now_mono, double push_silence_sec, double poll_min_sec, double *last_poll_mono,
    const std::function<bool()> &poll_fn) {
  FillResolveResult out;
  if (cache != NULL) {
    trade::OrderPushRow row;
    if (cache->GetRow(order_id, &row)) {
      const double last_push = cache->LastUpdateMono(order_id);
      if (last_push + 1e-9 >= placed_mono) {
        out.has_row = true;
        out.row = row;
        out.source = "push";
        return out;
      }
    } else {
      const double last_push = cache->LastUpdateMono(order_id);
      if (last_push > placed_mono || (now_mono - placed_mono) < push_silence_sec) {
        out.source = "none";
        return out;
      }
    }
  }

  if (poll_fn && last_poll_mono != NULL &&
      (now_mono - *last_poll_mono >= poll_min_sec || *last_poll_mono <= 0.0)) {
    *last_poll_mono = now_mono;
    if (poll_fn()) {
      if (cache != NULL) {
        trade::OrderPushRow row;
        if (cache->GetRow(order_id, &row)) {
          out.has_row = true;
          out.row = row;
          out.source = "poll";
        }
      }
    }
  }
  return out;
}

}  // namespace strategy
}  // namespace mmtrade
