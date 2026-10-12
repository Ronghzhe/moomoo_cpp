#include "strategy/net_split_leg_wait.hpp"

#include <cmath>

namespace mmtrade {
namespace strategy {

namespace {

constexpr double kQtyEps = 1e-9;

}  // namespace

bool IsStaleWaitStatus(const std::string &status) {
  if (status.find("SUBMITTED") != std::string::npos ||
      status.find("SUBMITTING") != std::string::npos ||
      status.find("WAITING_SUBMIT") != std::string::npos ||
      status.find("ENABLE") != std::string::npos) {
    return true;
  }
  return status == "1" || status == "2" || status == "3";
}

bool IsStaleReplaceOrderType(const std::string &order_type) {
  return order_type.find("SIM_MARKET") != std::string::npos ||
         order_type.find("NORMAL") != std::string::npos ||
         order_type.find("LIMIT") != std::string::npos ||
         order_type.find("MARKET") != std::string::npos;
}

bool ShouldNetSplitBookGuardStaleReprice(const NetSplitPendingSnapshot &pend,
                                         const std::string &status, double dealt_qty,
                                         double target_qty, double now_mono, double stale_sec,
                                         bool book_guard_enabled) {
  if (!book_guard_enabled) {
    return false;
  }
  if (!pend.book_guard_emergency || !pend.book_guard_emergency_limit) {
    return false;
  }
  if (pend.book_guard_emergency_market_tried) {
    return false;
  }
  if (stale_sec <= 0.0 || pend.placed_mono <= 0.0) {
    return false;
  }
  if (!IsStaleWaitStatus(status)) {
    return false;
  }
  if (dealt_qty + kQtyEps >= target_qty) {
    return false;
  }
  return (now_mono - pend.placed_mono) >= stale_sec;
}

bool ShouldNetSplitStaleReprice(const std::string &order_type, const std::string &status,
                                double dealt_qty, double target_qty, double placed_mono,
                                double now_mono, double stale_sec) {
  if (stale_sec <= 0.0 || !IsStaleReplaceOrderType(order_type)) {
    return false;
  }
  if (placed_mono <= 0.0) {
    return false;
  }
  if (!IsStaleWaitStatus(status)) {
    return false;
  }
  if (dealt_qty + kQtyEps >= target_qty) {
    return false;
  }
  return (now_mono - placed_mono) >= stale_sec;
}

void ResetNetSplitLegRetryState(NetSplitRetryState *rt, NetSplitScene scene) {
  if (rt == NULL) {
    return;
  }
  if (scene == NetSplitScene::Entry) {
    rt->entry_retry_failed_count = 0;
    rt->entry_retry_after_mono = 0.0;
    rt->entry_force_market_next = false;
    rt->entry_stale_replace_count = 0;
    rt->entry_book_guard_emergency_market_tried = false;
  } else {
    rt->exit_retry_failed_count = 0;
    rt->exit_retry_after_mono = 0.0;
    rt->exit_force_market_next = false;
    rt->exit_stale_replace_count = 0;
    rt->exit_book_guard_emergency_market_tried = false;
  }
}

double NetSplitRetryAfterMono(const NetSplitRetryState &rt, NetSplitScene scene) {
  return scene == NetSplitScene::Entry ? rt.entry_retry_after_mono : rt.exit_retry_after_mono;
}

}  // namespace strategy
}  // namespace mmtrade
