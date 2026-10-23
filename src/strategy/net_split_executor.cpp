#include "strategy/entry_net_split.hpp"

#include "opend/order_client.hpp"
#include "strategy/entry_net_planner.hpp"
#include "strategy/net_split_broker_query.hpp"
#include "strategy/net_split_config.hpp"
#include "strategy/net_split_executor.hpp"
#include "strategy/net_split_rollback.hpp"
#include "strategy/net_split_util.hpp"
#include "strategy/order_executor.hpp"
#include "strategy/order_fail_retry.hpp"
#include "strategy/order_pending_loop.hpp"
#include "strategy/position_sizing.hpp"
#include "strategy/fill_detector.hpp"
#include "strategy/book_guard.hpp"
#include "strategy/hk_short_params.hpp"
#include "strategy/net_split_leg_wait.hpp"
#include "strategy/symbol_tradability.hpp"
#include "trade/order_errors.hpp"
#include "trade/order_id_util.hpp"

#include <chrono>
#include <sstream>
#include <thread>

namespace mmtrade {
namespace strategy {

namespace {

std::string PendingOid(const PendingOrderState &pending) {
  return trade::ResolveDisplayOrderId(pending.order_id, pending.order_id_ex);
}

double NowMono() {
  const std::chrono::steady_clock::time_point tp = std::chrono::steady_clock::now();
  return std::chrono::duration<double>(tp.time_since_epoch()).count();
}

bool WaitForLegFill(SymbolLoopState *st, opend::OrderClient *client,
                    const config::StrategyConfig &cfg, double target_qty, double placed_mono,
                    double *last_poll_mono, NetSplitPendingSnapshot *pend_snap) {
  const double poll_min = cfg.order_poll_min_sec > 0.0 ? cfg.order_poll_min_sec : 3.0;
  const double stale_sec =
      cfg.order_unfilled_replace_sec > 0.0 ? cfg.order_unfilled_replace_sec : 12.0;
  const double deadline = placed_mono + 180.0;
  while (NowMono() < deadline) {
    const double now_mono = NowMono();
    const std::string oid = PendingOid(st->pending);
    const FillResolveResult resolved = ResolveOrderRowForFill(
        client->PushHub(), oid, placed_mono, now_mono, OrderFillPushSilenceSec(), poll_min,
        last_poll_mono,
        [&client, st]() {
          return client->QueryOrder(st->pending.order_id, st->sym.code, st->pending.order_id_ex);
        });
    if (!resolved.has_row) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      continue;
    }
    const FillClassification fill = ClassifyOrderFill(resolved.row, target_qty);
    if (fill.outcome == FillOutcome::kFilled) {
      return true;
    }
    if (fill.outcome == FillOutcome::kFailed) {
      return false;
    }
    if (pend_snap != NULL &&
        ShouldNetSplitBookGuardStaleReprice(*pend_snap, resolved.row.order_status, fill.dealt_qty,
                                            target_qty, now_mono, BookGuardEmergencyStaleSec(),
                                            BookGuardEmergencyEnabled())) {
      client->CancelOrder(st->pending.order_id, st->sym.code, st->pending.order_id_ex);
      std::this_thread::sleep_for(std::chrono::milliseconds(350));
      return false;
    }
    if (ShouldNetSplitStaleReprice(st->pending.order_type_used, resolved.row.order_status,
                                   fill.dealt_qty, target_qty, placed_mono, now_mono, stale_sec)) {
      client->CancelOrder(st->pending.order_id, st->sym.code, st->pending.order_id_ex);
      std::this_thread::sleep_for(std::chrono::milliseconds(350));
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  return false;
}

void HandleSubsequentLegFailure(SymbolLoopState *st, opend::OrderClient *client,
                                log::StrategyStore *store, const config::StrategyConfig &cfg,
                                const NetSplitSession &session, int failed_idx,
                                const EntryLeg &failed_leg, const std::string &fail_detail) {
  if (session.filled_legs.empty()) {
    return;
  }
  const std::pair<double, double> cur = BrokerLongShortQty(client, session.trade_code);
  const std::vector<RollbackGapOrder> gaps =
      RollbackGapOrders(session.pre_broker_long, session.pre_broker_short, cur.first, cur.second);
  bool rollback_ok = gaps.empty();
  for (size_t i = 0; i < gaps.size() && rollback_ok; ++i) {
    const PlaceAttemptResult pr = PlacePendingOrder(
        st, client, PendingKind::kEntry, gaps[i].first, gaps[i].second, 0.0, "MARKET", false,
        false, 0.0, store, "paper", "", "net_split_rollback");
    if (!pr.ok) {
      rollback_ok = false;
      break;
    }
    double poll_mono = 0.0;
    NetSplitPendingSnapshot snap;
    const double placed_mono = NowMono();
    st->pending.placed_mono = placed_mono;
    rollback_ok =
        WaitForLegFill(st, client, cfg, gaps[i].second, placed_mono, &poll_mono, &snap);
    st->entry_await_fill = false;
    st->exit_await_fill = false;
    st->pending = PendingOrderState();
  }
  RestoreSymStatusFromSession(&st->sym.status, &st->sym.opened_qty, &st->sym.opened_price,
                              &st->sym.closed_qty, &st->sym.closed_price, &st->sym.initial_qty,
                              &st->entry_done, session);
  st->entry_await_fill = false;
  st->exit_await_fill = false;
  st->pending = PendingOrderState();
  const std::string detail = FormatNetSplitFailDetail(
      session.scene, session.trade_code, failed_idx, failed_leg, fail_detail, session,
      rollback_ok, rollback_ok ? "ok" : "rollback_failed", cur.first, cur.second);
  if (store != NULL) {
    store->LogEvent("ERROR", rollback_ok ? "net_split_leg2_fail_rollback_ok"
                                         : "net_split_rollback_failed",
                    detail);
  }
  if (!rollback_ok) {
    st->entry_blocked = true;
    st->trading_fatal_code = "NET_SPLIT_ROLLBACK_FAILED";
    st->trading_fatal_detail = detail;
  }
}

EntryNetSplitParams ExitToEntryParams(const ExitNetSplitParams &params) {
  EntryNetSplitParams out;
  out.state = params.state;
  out.store = params.store;
  out.client = params.client;
  out.cfg = params.cfg;
  out.cfg_path = params.cfg_path;
  out.trd_env = params.trd_env;
  out.book = params.book;
  out.px = params.px;
  out.qty = params.exit_slice;
  out.remark = params.remark;
  out.book_guard_emergency = params.book_guard_emergency;
  out.in_rth = params.in_rth;
  return out;
}

}  // namespace

NetSplitLegFillResult ExecuteNetSplitLegSync(const EntryNetSplitParams &ctx, NetSplitScene scene,
                                             const EntryLeg &leg, int leg_idx, int leg_total,
                                             const std::string &entry_side_for_leg,
                                             bool book_guard_emergency,
                                             const std::string &exit_rule_id) {
  NetSplitLegFillResult out;
  if (ctx.state == NULL || ctx.client == NULL || ctx.cfg == NULL || ctx.book == NULL) {
    out.error = "missing_context";
    return out;
  }
  SymbolLoopState *st = ctx.state;
  const int retry_max = ctx.cfg->order_retry_max >= 0 ? ctx.cfg->order_retry_max : 3;
  const PendingKind kind =
      scene == NetSplitScene::Entry ? PendingKind::kEntry : PendingKind::kExit;
  const std::string leg_remark = NetSplitLegRemark(ctx.remark, leg, leg_idx, leg_total);
  const bool leg_short_open =
      LegIsHkShortOpen(st->sym.code, leg, entry_side_for_leg);

  ResetNetSplitLegRetryState(&st->net_split_retry, scene);
  for (int attempt = 0; attempt <= retry_max; ++attempt) {
    const bool market_fallback = attempt >= retry_max ||
                                 (scene == NetSplitScene::Entry
                                      ? st->net_split_retry.entry_force_market_next
                                      : st->net_split_retry.exit_force_market_next);
    std::string order_type = market_fallback ? "MARKET" : st->sym.order_type;
    bool bg_limit = book_guard_emergency && !market_fallback;
    if (book_guard_emergency && market_fallback) {
      order_type = "MARKET";
      bg_limit = false;
    } else if (book_guard_emergency) {
      order_type = "NORMAL";
    }

    double leg_ref = 0.0;
    double submit_px = 0.0;
    if (book_guard_emergency) {
      const EmergencyRefResult em = EmergencyRefPrice(
          leg.side, ctx.book->bid_1, ctx.book->ask_1, st->sym.opened_price, ctx.px);
      leg_ref = em.price;
      submit_px = bg_limit ? EmergencyLimitSubmitPx(st->sym.code, leg.side, leg_ref) : 0.0;
    } else {
      leg_ref = LegRefPx(leg, st->sym.code, entry_side_for_leg, ctx.px, ctx.book->bid_1,
                         ctx.book->ask_1);
      submit_px = ComputeSubmitPriceForAttempt(st->sym, *ctx.book, leg.side, attempt,
                                               market_fallback);
    }

    const PlaceAttemptResult pr = PlacePendingOrder(
        st, ctx.client, kind, leg.side, leg.qty, submit_px, order_type, book_guard_emergency,
        bg_limit, leg_ref, ctx.store, ctx.trd_env, exit_rule_id, leg_remark);
    if (!pr.ok) {
      if (IsWaitableSymbolNotTradableFail(pr.error)) {
        out.error = pr.error;
        return out;
      }
      if (!trade::IsOrderFailRetriable("", pr.error)) {
        st->entry_blocked = true;
        st->trading_fatal_code =
            scene == NetSplitScene::Entry ? "ENTRY_ORDER_FAILED" : "EXIT_ORDER_FAILED";
        st->trading_fatal_detail = pr.error;
        out.error = pr.error;
        return out;
      }
      if (scene == NetSplitScene::Entry) {
        st->net_split_retry.entry_retry_failed_count += 1;
        if (trade::IsRetryableTryAgainLater(pr.error)) {
          st->net_split_retry.entry_force_market_next = true;
        }
        if (attempt >= retry_max) {
          out.error = pr.error;
          return out;
        }
        if (book_guard_emergency && bg_limit) {
          st->net_split_retry.entry_book_guard_emergency_market_tried = true;
        }
        const double wait_sec = OrderFailRetryWaitSec(*ctx.cfg, "", pr.error);
        if (wait_sec > 0.0) {
          std::this_thread::sleep_for(
              std::chrono::milliseconds(static_cast<int>(wait_sec * 1000.0)));
        }
        continue;
      }
      st->net_split_retry.exit_retry_failed_count += 1;
      if (trade::IsRetryableTryAgainLater(pr.error)) {
        st->net_split_retry.exit_force_market_next = true;
      }
      if (attempt >= retry_max) {
        out.error = pr.error;
        return out;
      }
      const double wait_sec = OrderFailRetryWaitSec(*ctx.cfg, "", pr.error);
      if (wait_sec > 0.0) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(static_cast<int>(wait_sec * 1000.0)));
      }
      continue;
    }

    const double placed_mono = NowMono();
    st->pending.placed_mono = placed_mono;
    NetSplitPendingSnapshot pend_snap;
    pend_snap.book_guard_emergency = book_guard_emergency;
    pend_snap.book_guard_emergency_limit = bg_limit;
    pend_snap.book_guard_emergency_market_tried =
        book_guard_emergency && !bg_limit;
    pend_snap.placed_mono = placed_mono;

    double poll_mono = 0.0;
    if (!WaitForLegFill(st, ctx.client, *ctx.cfg, leg.qty, placed_mono, &poll_mono, &pend_snap)) {
      if (scene == NetSplitScene::Entry) {
        st->net_split_retry.entry_retry_failed_count += 1;
        if (book_guard_emergency && bg_limit) {
          st->net_split_retry.entry_book_guard_emergency_market_tried = true;
        } else if (attempt + 1 >= 3) {
          st->net_split_retry.entry_force_market_next = true;
        }
      } else {
        st->net_split_retry.exit_retry_failed_count += 1;
        if (attempt + 1 >= 3) {
          st->net_split_retry.exit_force_market_next = true;
        }
      }
      st->entry_await_fill = false;
      st->exit_await_fill = false;
      if (attempt >= retry_max) {
        out.error = "leg_not_filled";
        return out;
      }
      continue;
    }

    const FillResolveResult resolved = ResolveOrderRowForFill(
        ctx.client->PushHub(), PendingOid(st->pending), placed_mono, NowMono(),
        OrderFillPushSilenceSec(),
        ctx.cfg->order_poll_min_sec > 0.0 ? ctx.cfg->order_poll_min_sec : 3.0, &poll_mono,
        [&ctx, st]() {
          return ctx.client->QueryOrder(st->pending.order_id, st->sym.code, st->pending.order_id_ex);
        });
    const FillClassification fill = ClassifyOrderFill(resolved.row, leg.qty);
    out.ok = true;
    out.fill_qty = fill.dealt_qty > 0.0 ? fill.dealt_qty : leg.qty;
    out.fill_px = fill.dealt_avg_price > 0.0 ? fill.dealt_avg_price : leg_ref;
    out.fill_status = resolved.row.order_status;
    out.oid = PendingOid(st->pending);
    st->entry_await_fill = false;
    st->exit_await_fill = false;
    st->pending = PendingOrderState();
    (void)leg_short_open;
    return out;
  }
  out.error = "retry_exhausted";
  return out;
}

bool TryExecuteEntryNetSplit(const EntryNetSplitParams &params) {
  if (!CppNetSplitActiveForCode(params.state != NULL ? params.state->sym.code : "")) {
    return false;
  }
  SymbolLoopState *st = params.state;
  if (st == NULL || params.client == NULL || params.cfg == NULL || params.book == NULL) {
    return false;
  }

  const std::pair<double, double> broker = BrokerLongShortQty(params.client, st->sym.code);
  const std::vector<EntryLeg> legs =
      PlanEntryLegs(st->sym.entry_side, params.qty, broker.first, broker.second);
  if (!NeedsNetSplit(legs)) {
    return false;
  }

  if (params.store != NULL) {
    params.store->LogEvent("INFO", "entry_net_split_plan",
                           "legs=" + std::to_string(legs.size()));
  }

  const bool multi_leg = legs.size() > 1;
  NetSplitSession session;
  if (multi_leg) {
    session = CaptureNetSplitSession(
        NetSplitScene::Entry, st->sym.code, params.cfg_path, static_cast<int>(legs.size()),
        broker.first, broker.second, st->sym.status, st->sym.opened_qty, st->sym.opened_price,
        st->sym.closed_qty, st->sym.closed_price, st->sym.initial_qty, st->entry_done);
  }

  for (size_t i = 0; i < legs.size(); ++i) {
    const NetSplitLegFillResult lr = ExecuteNetSplitLegSync(
        params, NetSplitScene::Entry, legs[i], static_cast<int>(i),
        static_cast<int>(legs.size()), st->sym.entry_side, params.book_guard_emergency);
    if (!lr.ok) {
      if (multi_leg && !session.filled_legs.empty()) {
        HandleSubsequentLegFailure(st, params.client, params.store, *params.cfg, session,
                                   static_cast<int>(i), legs[i], lr.error);
      }
      return true;
    }
    if (multi_leg) {
      AppendNetSplitLegFill(&session, static_cast<int>(i), legs[i], lr.fill_qty, lr.fill_px,
                            lr.oid);
    }
    ApplyEntryFill(st, lr.fill_qty, lr.fill_px, st->lot_size);
    if (params.store != NULL) {
      params.store->LogEvent("INFO", "entry_net_split_leg_filled",
                             "leg=" + std::to_string(i + 1) + " oid=" + lr.oid);
    }
  }
  return true;
}

bool TryExecuteExitNetSplit(const ExitNetSplitParams &params) {
  if (!CppNetSplitActiveForCode(params.state != NULL ? params.state->sym.code : "")) {
    return false;
  }
  SymbolLoopState *st = params.state;
  if (st == NULL || params.client == NULL || params.cfg == NULL || params.book == NULL) {
    return false;
  }

  const std::pair<double, double> broker = BrokerLongShortQty(params.client, st->sym.code);
  const std::vector<EntryLeg> legs =
      PlanExitLegs(st->sym.entry_side, params.exit_slice, broker.first, broker.second);
  if (!NeedsNetSplit(legs)) {
    return false;
  }

  if (params.store != NULL) {
    params.store->LogEvent("INFO", "exit_net_split_plan", "legs=" + std::to_string(legs.size()));
  }

  const bool multi_leg = legs.size() > 1;
  NetSplitSession session;
  if (multi_leg) {
    session = CaptureNetSplitSession(
        NetSplitScene::Exit, st->sym.code, params.cfg_path, static_cast<int>(legs.size()),
        broker.first, broker.second, st->sym.status, st->sym.opened_qty, st->sym.opened_price,
        st->sym.closed_qty, st->sym.closed_price, st->sym.initial_qty, st->entry_done);
  }

  for (size_t i = 0; i < legs.size(); ++i) {
    const std::string entry_side_for_leg =
        ExitEntrySideForLeg(st->sym.entry_side, legs[i].purpose);
    const EntryNetSplitParams leg_ctx = ExitToEntryParams(params);
    const NetSplitLegFillResult lr = ExecuteNetSplitLegSync(
        leg_ctx, NetSplitScene::Exit, legs[i], static_cast<int>(i),
        static_cast<int>(legs.size()), entry_side_for_leg, params.book_guard_emergency,
        params.exit_rule_id);
    if (!lr.ok) {
      if (multi_leg && !session.filled_legs.empty()) {
        HandleSubsequentLegFailure(st, params.client, params.store, *params.cfg, session,
                                   static_cast<int>(i), legs[i], lr.error);
      }
      return true;
    }
    if (multi_leg) {
      AppendNetSplitLegFill(&session, static_cast<int>(i), legs[i], lr.fill_qty, lr.fill_px,
                            lr.oid);
    }
    ApplyExitFill(st, lr.fill_qty, lr.fill_px);
    if (params.store != NULL) {
      params.store->LogEvent("INFO", "exit_net_split_leg_filled",
                             "leg=" + std::to_string(i + 1) + " oid=" + lr.oid);
    }
  }
  return true;
}

}  // namespace strategy
}  // namespace mmtrade
