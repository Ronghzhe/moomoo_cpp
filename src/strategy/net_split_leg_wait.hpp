#pragma once

#include "strategy/net_split_rollback.hpp"

#include <string>

namespace mmtrade {
namespace strategy {

enum class NetSplitWaitStatus {
  Wait,
  Filled,
  Failed,
  Reprice,
  Fatal,
  QueryFail,
};

struct NetSplitWaitResult {
  NetSplitWaitStatus status;
  double fill_qty;
  double fill_px;
  std::string fill_status;
  std::string fail_detail;
  std::string oid;

  NetSplitWaitResult()
      : status(NetSplitWaitStatus::Wait),
        fill_qty(0.0),
        fill_px(0.0) {}
};

struct NetSplitRetryState {
  int entry_retry_failed_count;
  double entry_retry_after_mono;
  bool entry_force_market_next;
  int entry_stale_replace_count;
  bool entry_book_guard_emergency_market_tried;

  int exit_retry_failed_count;
  double exit_retry_after_mono;
  bool exit_force_market_next;
  int exit_stale_replace_count;
  bool exit_book_guard_emergency_market_tried;

  NetSplitRetryState()
      : entry_retry_failed_count(0),
        entry_retry_after_mono(0.0),
        entry_force_market_next(false),
        entry_stale_replace_count(0),
        entry_book_guard_emergency_market_tried(false),
        exit_retry_failed_count(0),
        exit_retry_after_mono(0.0),
        exit_force_market_next(false),
        exit_stale_replace_count(0),
        exit_book_guard_emergency_market_tried(false) {}
};

struct NetSplitPendingSnapshot {
  bool book_guard_emergency;
  bool book_guard_emergency_limit;
  bool book_guard_emergency_market_tried;
  double placed_mono;
};

bool IsStaleWaitStatus(const std::string &status);

bool IsStaleReplaceOrderType(const std::string &order_type);

bool ShouldNetSplitBookGuardStaleReprice(const NetSplitPendingSnapshot &pend,
                                         const std::string &status, double dealt_qty,
                                         double target_qty, double now_mono, double stale_sec,
                                         bool book_guard_enabled);

bool ShouldNetSplitStaleReprice(const std::string &order_type, const std::string &status,
                                double dealt_qty, double target_qty, double placed_mono,
                                double now_mono, double stale_sec);

void ResetNetSplitLegRetryState(NetSplitRetryState *rt, NetSplitScene scene);

double NetSplitRetryAfterMono(const NetSplitRetryState &rt, NetSplitScene scene);

}  // namespace strategy
}  // namespace mmtrade
