#include "strategy/net_split_rollback.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace mmtrade {
namespace strategy {

namespace {

constexpr double kQtyEps = 1e-9;

std::string SceneTag(NetSplitScene scene) {
  return scene == NetSplitScene::Entry ? "ENTRY" : "EXIT";
}

}  // namespace

std::vector<RollbackGapOrder> RollbackGapOrders(double pre_long, double pre_short,
                                                double cur_long, double cur_short) {
  std::vector<RollbackGapOrder> orders;

  if (cur_long + kQtyEps < pre_long) {
    orders.emplace_back("BUY", pre_long - cur_long);
  } else if (cur_long > pre_long + kQtyEps) {
    orders.emplace_back("SELL", cur_long - pre_long);
  }

  if (cur_short > pre_short + kQtyEps) {
    orders.emplace_back("BUY", cur_short - pre_short);
  } else if (cur_short + kQtyEps < pre_short) {
    orders.emplace_back("SELL", pre_short - cur_short);
  }

  orders.erase(std::remove_if(orders.begin(), orders.end(),
                              [](const RollbackGapOrder &o) { return o.second <= kQtyEps; }),
               orders.end());
  return orders;
}

std::pair<double, double> AggregateLegFillsVwap(
    const std::vector<NetSplitLegFill> &fills) {
  double total = 0.0;
  double notional = 0.0;
  for (const auto &f : fills) {
    total += f.qty;
    notional += f.qty * f.px;
  }
  if (total <= kQtyEps) {
    return {0.0, 0.0};
  }
  return {total, notional / total};
}

NetSplitSession CaptureNetSplitSession(NetSplitScene scene, const std::string &trade_code,
                                       const std::string &cfg_path, int leg_total,
                                       double pre_broker_long, double pre_broker_short,
                                       int pre_sym_status, double pre_opened_qty,
                                       double pre_opened_price, double pre_closed_qty,
                                       double pre_closed_price, double pre_initial_qty,
                                       bool pre_entry_done) {
  NetSplitSession session;
  session.scene = scene;
  session.trade_code = trade_code;
  session.cfg_path = cfg_path;
  session.leg_total = leg_total;
  session.pre_broker_long = pre_broker_long;
  session.pre_broker_short = pre_broker_short;
  session.pre_sym_status = pre_sym_status;
  session.pre_opened_qty = pre_opened_qty;
  session.pre_opened_price = pre_opened_price;
  session.pre_closed_qty = pre_closed_qty;
  session.pre_closed_price = pre_closed_price;
  session.pre_initial_qty = pre_initial_qty;
  session.pre_entry_done = pre_entry_done;
  return session;
}

void AppendNetSplitLegFill(NetSplitSession *session, int idx, const EntryLeg &leg,
                           double fill_qty, double fill_px, const std::string &oid) {
  if (session == NULL) {
    return;
  }
  NetSplitLegFill fill;
  fill.idx = idx;
  fill.purpose = LegPurposeToString(leg.purpose);
  fill.side = leg.side;
  fill.qty = fill_qty;
  fill.px = fill_px;
  fill.oid = oid;
  session->filled_legs.push_back(fill);
}

void RestoreSymStatusFromSession(int *sym_status, double *opened_qty, double *opened_price,
                                 double *closed_qty, double *closed_price, double *initial_qty,
                                 bool *entry_done, const NetSplitSession &session) {
  if (sym_status != NULL) {
    *sym_status = session.pre_sym_status;
  }
  if (opened_qty != NULL) {
    *opened_qty = session.pre_opened_qty;
  }
  if (opened_price != NULL) {
    *opened_price = session.pre_opened_price;
  }
  if (closed_qty != NULL) {
    *closed_qty = session.pre_closed_qty;
  }
  if (closed_price != NULL) {
    *closed_price = session.pre_closed_price;
  }
  if (initial_qty != NULL) {
    *initial_qty = session.pre_initial_qty;
  }
  if (entry_done != NULL) {
    *entry_done = session.pre_entry_done;
  }
}

std::string FormatNetSplitFailDetail(NetSplitScene scene, const std::string &trade_code,
                                     int failed_leg_idx, const EntryLeg &failed_leg,
                                     const std::string &fail_detail,
                                     const NetSplitSession &session, bool rollback_ok,
                                     const std::string &rollback_note,
                                     double broker_long_after, double broker_short_after) {
  std::ostringstream leg1_part;
  if (!session.filled_legs.empty()) {
    const auto &leg1 = session.filled_legs.front();
    leg1_part << "leg1=" << leg1.purpose << ":" << leg1.side << ":" << leg1.qty << "@"
              << leg1.px << " oid=" << leg1.oid;
  } else {
    leg1_part << "leg1=none";
  }

  const std::string fail_trim =
      fail_detail.size() > 200 ? fail_detail.substr(0, 200) : fail_detail;
  const std::string rb_note_trim =
      rollback_note.size() > 120 ? rollback_note.substr(0, 120) : rollback_note;
  const std::string rb = rollback_ok ? "rollback_ok" : "rollback_fail";

  std::ostringstream oss;
  oss << "NET_SPLIT_" << SceneTag(scene) << "_LEG" << (failed_leg_idx + 1) << "_FAIL | "
      << trade_code << " | fail=" << fail_trim << " | leg" << (failed_leg_idx + 1) << "="
      << LegPurposeToString(failed_leg.purpose) << ":" << failed_leg.side << ":"
      << failed_leg.qty << " | " << leg1_part.str()
      << " | broker_before=long" << session.pre_broker_long << "/short"
      << session.pre_broker_short << " | broker_after=long" << broker_long_after
      << "/short" << broker_short_after << " | " << rb << "=" << rb_note_trim;

  std::string detail = oss.str();
  if (detail.size() > 480) {
    detail.resize(480);
  }
  return detail;
}

}  // namespace strategy
}  // namespace mmtrade
