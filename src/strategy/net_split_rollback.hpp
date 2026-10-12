#pragma once

#include "strategy/entry_net_planner.hpp"

#include <string>
#include <utility>
#include <vector>

namespace mmtrade {
namespace strategy {

enum class NetSplitScene { Entry, Exit };

struct NetSplitLegFill {
  int idx = 0;
  std::string purpose;
  std::string side;
  double qty = 0.0;
  double px = 0.0;
  std::string oid;
};

struct NetSplitSession {
  NetSplitScene scene = NetSplitScene::Entry;
  std::string trade_code;
  std::string cfg_path;
  int leg_total = 0;
  double pre_broker_long = 0.0;
  double pre_broker_short = 0.0;
  int pre_sym_status = 0;
  double pre_opened_qty = 0.0;
  double pre_opened_price = 0.0;
  double pre_closed_qty = 0.0;
  double pre_closed_price = 0.0;
  double pre_initial_qty = 0.0;
  bool pre_entry_done = false;
  double pre_position_qty = 0.0;
  std::vector<NetSplitLegFill> filled_legs;
};

using RollbackGapOrder = std::pair<std::string, double>;

std::vector<RollbackGapOrder> RollbackGapOrders(double pre_long, double pre_short,
                                                double cur_long, double cur_short);

std::pair<double, double> AggregateLegFillsVwap(
    const std::vector<NetSplitLegFill> &fills);

NetSplitSession CaptureNetSplitSession(NetSplitScene scene, const std::string &trade_code,
                                       const std::string &cfg_path, int leg_total,
                                       double pre_broker_long, double pre_broker_short,
                                       int pre_sym_status, double pre_opened_qty,
                                       double pre_opened_price, double pre_closed_qty,
                                       double pre_closed_price, double pre_initial_qty,
                                       bool pre_entry_done);

void AppendNetSplitLegFill(NetSplitSession *session, int idx, const EntryLeg &leg,
                           double fill_qty, double fill_px, const std::string &oid);

void RestoreSymStatusFromSession(int *sym_status, double *opened_qty, double *opened_price,
                                 double *closed_qty, double *closed_price, double *initial_qty,
                                 bool *entry_done, const NetSplitSession &session);

std::string FormatNetSplitFailDetail(NetSplitScene scene, const std::string &trade_code,
                                     int failed_leg_idx, const EntryLeg &failed_leg,
                                     const std::string &fail_detail,
                                     const NetSplitSession &session, bool rollback_ok,
                                     const std::string &rollback_note,
                                     double broker_long_after, double broker_short_after);

}  // namespace strategy
}  // namespace mmtrade
