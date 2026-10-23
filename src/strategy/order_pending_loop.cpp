#include "strategy/order_pending_loop.hpp"

#include "config/config_persist.hpp"
#include "log/strategy_console_log.hpp"
#include "log/strategy_message_format.hpp"
#include "log/strategy_runtime_log.hpp"
#include "log/strategy_store.hpp"
#include "notify/strategy_notify.hpp"
#include "opend/order_client.hpp"
#include "strategy/book_guard.hpp"
#include "strategy/entry_net_split.hpp"
#include "strategy/fill_detector.hpp"
#include "strategy/hk_short_params.hpp"
#include "strategy/net_split_leg_wait.hpp"
#include "strategy/order_executor.hpp"
#include "strategy/order_fail_retry.hpp"
#include "strategy/order_session_policy.hpp"
#include "strategy/position_sizing.hpp"
#include "strategy/symbol_tradability.hpp"
#include "task/pipeline_latency.hpp"
#include "task/task_latency_recorder.hpp"
#include "trade/order_errors.hpp"
#include "trade/order_id_util.hpp"

#include <cmath>
#include <iostream>
#include <sstream>
#include <thread>

namespace mmtrade {
namespace strategy {

namespace {

std::string PendingDisplayOrderId(const PendingOrderState &pending) {
  return trade::ResolveDisplayOrderId(pending.order_id, pending.order_id_ex);
}

std::string ResolveExitSide(const config::SymbolConfig &sym, const std::string &exit_rule_id) {
  for (size_t i = 0; i < sym.exit_strategies.size(); ++i) {
    if (sym.exit_strategies[i].rule_id == exit_rule_id) {
      return sym.exit_strategies[i].side;
    }
  }
  return sym.exit.enabled ? sym.exit.side : "SELL";
}

std::string FormatProfitPct(double pct) {
  std::ostringstream oss;
  oss.setf(std::ios::fixed);
  oss.precision(2);
  oss << (pct >= 0.0 ? "+" : "") << pct << "%";
  return oss.str();
}

std::string FormatProfitAmount(double amt) {
  std::ostringstream oss;
  oss.setf(std::ios::fixed);
  oss.precision(2);
  oss << (amt >= 0.0 ? "+" : "") << amt;
  return oss.str();
}

void PersistRuntimeQuiet(const std::string &config_path, const config::SymbolConfig &sym,
                         log::StrategyStore *store) {
  try {
    config::PersistStrategyConfigRuntime(config_path, sym);
  } catch (const std::exception &e) {
    if (store != NULL) {
      store->LogEvent("WARN", "persist_runtime_failed", e.what());
    }
  }
}

void NotifyFill(const FillHooks *hooks, SymbolLoopState *st, PendingKind kind,
                const std::string &oid, double fill_qty, double fill_px, double submit_px) {
  if (hooks == NULL || hooks->cfg == NULL || hooks->store == NULL) {
    return;
  }
  if (!notify::TelegramStrategyEnabled(*hooks->cfg)) {
    return;
  }
  std::string msg;
  if (kind == PendingKind::kEntry) {
    msg = log::FormatEntryFillMessage(*hooks->cfg, st->sym, fill_qty, fill_px, submit_px, oid,
                                      hooks->trd_env, st->last_entry_hit_rule_id);
  } else {
    const std::string exit_rule = st->pending.exit_rule_id;
    msg = log::FormatExitFillMessage(*hooks->cfg, st->sym, st->pending.side, fill_qty, fill_px,
                                     submit_px, st->sym.opened_price, oid, exit_rule,
                                     hooks->trd_env);
  }
  const notify::NotifyResult nr = notify::SendStrategyNotifyText(*hooks->cfg, msg);
  if (!nr.ok) {
    hooks->store->LogEvent("WARN", "telegram_fill_failed", nr.detail);
    log::LogConsoleWarn("Telegram 成交通知失败: " + nr.detail);
  }
}

void UpdateOrderOnFill(const FillHooks *hooks, SymbolLoopState *st, PendingKind kind,
                       const std::string &oid, const std::string &status, double fill_qty,
                       double fill_px) {
  if (hooks == NULL || hooks->store == NULL) {
    return;
  }
  if (!hooks->store->UpdateOrderFill(oid, status, fill_px, fill_qty)) {
    hooks->store->SaveOrder(
        st->sym.task_id, st->sym.code, st->pending.side, oid, status, fill_qty, fill_px,
        kind == PendingKind::kEntry ? "strategy_cpp_entry_fill_backfill"
                                    : "strategy_cpp_exit_fill_backfill",
        "", "", "fill_backfill", hooks->trd_env);
  }
  if (kind == PendingKind::kExit) {
    const double entry_px = st->sym.opened_price;
    double profit_amount = 0.0;
    if (entry_px > 0.0) {
      if (st->sym.entry_side == "BUY") {
        profit_amount = (fill_px - entry_px) * fill_qty;
      } else {
        profit_amount = (entry_px - fill_px) * fill_qty;
      }
    }
    const double basis = std::abs(entry_px * fill_qty);
    const double profit_pct = basis > 1e-12 ? (profit_amount / basis * 100.0) : 0.0;
    std::string profit_status = "flat";
    if (profit_amount > 1e-9) {
      profit_status = "profit";
    } else if (profit_amount < -1e-9) {
      profit_status = "loss";
    }
    const std::string env_tag = hooks->trd_env == "paper" ? "SIMULATE" : "REAL";
    hooks->store->UpdateOrderProfit(oid, env_tag, profit_status, FormatProfitPct(profit_pct),
                                    FormatProfitAmount(profit_amount));
  }
}

void ScheduleNotTradableWait(SymbolLoopState *st, const std::string &detail, double now_mono,
                           log::StrategyStore *store) {
  const double wait_sec = SymbolTradabilityPollSec();
  st->place_cooldown_until_mono = now_mono + wait_sec;
  st->symbol_tradable = false;
  st->symbol_tradable_reason = detail;
  if (store != NULL) {
    store->LogEvent("WARN", "symbol_not_tradable_wait",
                    st->sym.code + " wait_sec=" + std::to_string(wait_sec) + " " + detail);
  }
  std::cerr << "symbol_not_tradable_wait code=" << st->sym.code << " detail=" << detail << "\n";
}

bool TryBookGuardEmergencyEntry(SymbolLoopState *st, mmtrade::opend::OrderClient *client,
                                const BookTick &book, const opend::QuoteSnapshot &snap,
                                double now_mono, double qty, log::StrategyStore *store,
                                const std::string &trd_env) {
  if (!BookGuardEmergencyEnabled() || st->book_guard.entry_book_guard_emergency_started) {
    return false;
  }
  const EmergencyRefResult ref = EmergencyRefPrice(
      st->sym.entry_side, book.bid_1, book.ask_1, st->sym.opened_price, snap.last_price);
  if (ref.price <= 0.0) {
    st->book_guard.entry_book_guard_emergency_started = true;
    return false;
  }
  st->book_guard.entry_book_guard_emergency_started = true;
  const double submit_px = EmergencyLimitSubmitPx(st->sym.code, st->sym.entry_side, ref.price);
  PlaceAttemptResult pr =
      PlacePendingOrder(st, client, PendingKind::kEntry, st->sym.entry_side, qty, submit_px, "NORMAL",
                    true, true, ref.price, store, trd_env, "");
  if (pr.ok) {
    st->pending.placed_mono = now_mono;
    if (store != NULL) {
      store->LogEvent("INFO", "book_guard_emergency_entry_limit", st->sym.code);
    }
    return true;
  }
  if (IsWaitableSymbolNotTradableFail(pr.error)) {
    ScheduleNotTradableWait(st, pr.error, now_mono, store);
    return false;
  }
  if (st->book_guard.entry_book_guard_emergency_market_tried) {
    return false;
  }
  st->book_guard.entry_book_guard_emergency_market_tried = true;
  pr = PlacePendingOrder(st, client, PendingKind::kEntry, st->sym.entry_side, qty, 0.0, "MARKET", true,
                   false, ref.price, store, trd_env, "");
  if (pr.ok) {
    st->pending.placed_mono = now_mono;
    if (store != NULL) {
      store->LogEvent("WARN", "book_guard_emergency_entry_market", st->sym.code);
    }
    return true;
  }
  if (IsWaitableSymbolNotTradableFail(pr.error)) {
    ScheduleNotTradableWait(st, pr.error, now_mono, store);
  }
  return pr.ok;
}

bool TryBookGuardEmergencyExit(SymbolLoopState *st, mmtrade::opend::OrderClient *client,
                               const BookTick &book, const opend::QuoteSnapshot &snap,
                               double now_mono, const std::string &exit_side, double qty,
                               const std::string &exit_rule_id, log::StrategyStore *store,
                               const std::string &trd_env) {
  if (!BookGuardEmergencyEnabled() || st->book_guard.exit_book_guard_emergency_started) {
    return false;
  }
  const EmergencyRefResult ref =
      EmergencyRefPrice(exit_side, book.bid_1, book.ask_1, st->sym.opened_price, snap.last_price);
  if (ref.price <= 0.0) {
    st->book_guard.exit_book_guard_emergency_started = true;
    return false;
  }
  st->book_guard.exit_book_guard_emergency_started = true;
  const double submit_px = EmergencyLimitSubmitPx(st->sym.code, exit_side, ref.price);
  PlaceAttemptResult pr =
      PlacePendingOrder(st, client, PendingKind::kExit, exit_side, qty, submit_px, "NORMAL", true,
                    true, ref.price, store, trd_env, exit_rule_id);
  if (pr.ok) {
    st->pending.placed_mono = now_mono;
    if (store != NULL) {
      store->LogEvent("INFO", "book_guard_emergency_exit_limit", st->sym.code);
    }
    return true;
  }
  if (IsWaitableSymbolNotTradableFail(pr.error)) {
    ScheduleNotTradableWait(st, pr.error, now_mono, store);
    return false;
  }
  if (st->book_guard.exit_book_guard_emergency_market_tried) {
    return false;
  }
  st->book_guard.exit_book_guard_emergency_market_tried = true;
  pr = PlacePendingOrder(st, client, PendingKind::kExit, exit_side, qty, 0.0, "MARKET", true, false,
                     ref.price, store, trd_env, exit_rule_id);
  if (pr.ok) {
    st->pending.placed_mono = now_mono;
    if (store != NULL) {
      store->LogEvent("WARN", "book_guard_emergency_exit_market", st->sym.code);
    }
    return true;
  }
  if (IsWaitableSymbolNotTradableFail(pr.error)) {
    ScheduleNotTradableWait(st, pr.error, now_mono, store);
  }
  return pr.ok;
}

bool HkShortEntryBandOk(SymbolLoopState *st, const BookTick &book, double now_ts,
                        log::StrategyStore *store) {
  if (!IsHkShortOpenEntry(st->sym.code, st->sym.entry_side, st->sym.entry_side)) {
    return true;
  }
  const int attempt = st->hk_short_place_attempt;
  if (attempt <= 0) {
    if (!HkShortBidInAskBand(book.bid_1, book.ask_1)) {
      if (now_ts - st->last_hk_short_band_skip_log_ts >= 5.0) {
        double lo = 0.0;
        double hi = 0.0;
        HkShortAskBand(book.ask_1, &lo, &hi);
        if (store != NULL) {
          store->LogEvent("INFO", "hk_short_ask_band_skip",
                          st->sym.code + " bid=" + std::to_string(book.bid_1) + " ask=" +
                              std::to_string(book.ask_1));
        }
        st->last_hk_short_band_skip_log_ts = now_ts;
      }
      return false;
    }
    if (book.ask_1 <= 0.0) {
      return false;
    }
    HkShortAskBand(book.ask_1, &st->hk_short_band_lo, &st->hk_short_band_hi);
    st->hk_short_band_ref_ask = book.ask_1;
    st->hk_short_place_attempt = 1;
    return true;
  }
  if (!HkShortAskInOriginalBand(book.ask_1, st->hk_short_band_lo, st->hk_short_band_hi)) {
    const std::string detail =
        "HK short out of band " +
        HkShortPriceOutOfBandDetail(book.ask_1, st->hk_short_band_lo, st->hk_short_band_hi,
                                    st->hk_short_band_ref_ask);
    st->entry_blocked = true;
    st->trading_fatal_code = "HK_SHORT_ENTRY_UNFILLED";
    st->trading_fatal_detail = detail;
    if (store != NULL) {
      store->LogEvent("ERROR", "hk_short_entry_unfilled", detail);
    }
    std::cerr << "hk_short_entry_unfilled code=" << st->sym.code << " " << detail << "\n";
    return false;
  }
  return true;
}

bool EntryBookGuardEmergencyStaleToMarket(SymbolLoopState *st, mmtrade::opend::OrderClient *client,
                                          const FillResolveResult &resolved, double now_mono,
                                          log::StrategyStore *store, const std::string &trd_env) {
  if (!BookGuardEmergencyEnabled() || !st->pending.book_guard_emergency_limit ||
      st->book_guard.entry_book_guard_emergency_market_tried) {
    return false;
  }
  const double stale_sec = BookGuardEmergencyStaleSec();
  if (stale_sec <= 0.0 || st->pending.placed_mono <= 0.0) {
    return false;
  }
  if (!IsStaleWaitStatus(resolved.row.order_status)) {
    return false;
  }
  const FillClassification fill = ClassifyOrderFill(resolved.row, st->pending.target_qty);
  if (fill.dealt_qty + 1e-9 >= st->pending.target_qty) {
    return false;
  }
  if ((now_mono - st->pending.placed_mono) < stale_sec) {
    return false;
  }
  client->CancelOrder(st->pending.order_id, st->sym.code, st->pending.order_id_ex);
  std::this_thread::sleep_for(std::chrono::milliseconds(
      static_cast<int>(300.0)));  // brief cancel confirm
  const double ref_px = st->pending.ref_px;
  const double qty = st->pending.target_qty;
  const std::string side = st->pending.side;
  st->pending = PendingOrderState();
  st->entry_await_fill = false;
  st->book_guard.entry_book_guard_emergency_market_tried = true;
  PlaceAttemptResult pr =
      PlacePendingOrder(st, client, PendingKind::kEntry, side, qty, 0.0, "MARKET", true, false, ref_px,
                    store, trd_env, "");
  if (pr.ok) {
    st->pending.placed_mono = now_mono;
    if (store != NULL) {
      store->LogEvent("WARN", "book_guard_emergency_entry_stale_market", st->sym.code);
    }
  } else if (IsWaitableSymbolNotTradableFail(pr.error)) {
    ScheduleNotTradableWait(st, pr.error, now_mono, store);
  }
  return false;
}

bool ExitBookGuardEmergencyStaleToMarket(SymbolLoopState *st, mmtrade::opend::OrderClient *client,
                                         const FillResolveResult &resolved, double now_mono,
                                         log::StrategyStore *store, const std::string &trd_env) {
  if (!BookGuardEmergencyEnabled() || !st->pending.book_guard_emergency_limit ||
      st->book_guard.exit_book_guard_emergency_market_tried) {
    return false;
  }
  const double stale_sec = BookGuardEmergencyStaleSec();
  if (stale_sec <= 0.0 || st->pending.placed_mono <= 0.0) {
    return false;
  }
  if (!IsStaleWaitStatus(resolved.row.order_status)) {
    return false;
  }
  const FillClassification fill = ClassifyOrderFill(resolved.row, st->pending.target_qty);
  if (fill.dealt_qty + 1e-9 >= st->pending.target_qty) {
    return false;
  }
  if ((now_mono - st->pending.placed_mono) < stale_sec) {
    return false;
  }
  client->CancelOrder(st->pending.order_id, st->sym.code, st->pending.order_id_ex);
  std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(300.0)));
  const double ref_px = st->pending.ref_px;
  const double qty = st->pending.target_qty;
  const std::string side = st->pending.side;
  const std::string exit_rule = st->pending.exit_rule_id;
  st->pending = PendingOrderState();
  st->exit_await_fill = false;
  st->book_guard.exit_book_guard_emergency_market_tried = true;
  PlaceAttemptResult pr =
      PlacePendingOrder(st, client, PendingKind::kExit, side, qty, 0.0, "MARKET", true, false, ref_px,
                    store, trd_env, exit_rule);
  if (pr.ok) {
    st->pending.placed_mono = now_mono;
    if (store != NULL) {
      store->LogEvent("WARN", "book_guard_emergency_exit_stale_market", st->sym.code);
    }
  } else if (IsWaitableSymbolNotTradableFail(pr.error)) {
    ScheduleNotTradableWait(st, pr.error, now_mono, store);
  }
  return false;
}

bool EntryHkShortUnfilledReplace(SymbolLoopState *st, mmtrade::opend::OrderClient *client,
                                 const config::StrategyConfig &cfg, const BookTick &book,
                                 const FillResolveResult &resolved, double now_mono,
                                 log::StrategyStore *store, const std::string &trd_env) {
  if (!IsHkShortOpenEntry(st->sym.code, st->sym.entry_side, st->sym.entry_side)) {
    return false;
  }
  if (st->pending.kind != PendingKind::kEntry) {
    return false;
  }
  if (now_mono < st->hk_short_reprice_pause_until) {
    return false;
  }
  const double wait_sec = kHkShortRepriceWaitSec;
  if (wait_sec <= 0.0 || st->pending.placed_mono <= 0.0) {
    return false;
  }
  if (!IsStaleWaitStatus(resolved.row.order_status)) {
    return false;
  }
  const FillClassification fill = ClassifyOrderFill(resolved.row, st->pending.target_qty);
  if (fill.dealt_qty + 1e-9 >= st->pending.target_qty) {
    return false;
  }
  if ((now_mono - st->pending.placed_mono) < wait_sec) {
    return false;
  }
  const int attempt = st->pending.hk_short_place_attempt > 0 ? st->pending.hk_short_place_attempt
                                                             : st->hk_short_place_attempt;
  if (attempt >= kHkShortMaxPlaceAttempts) {
    st->entry_blocked = true;
    st->trading_fatal_code = "HK_SHORT_ENTRY_UNFILLED";
    st->trading_fatal_detail = "HK short max reprice attempts";
    if (store != NULL) {
      store->LogEvent("ERROR", "hk_short_entry_unfilled", st->sym.code);
    }
    return true;
  }
  client->CancelOrder(st->pending.order_id, st->sym.code, st->pending.order_id_ex);
  std::this_thread::sleep_for(
      std::chrono::milliseconds(static_cast<int>(kHkShortPostCancelWaitSec * 1000.0)));
  double fresh_ask = book.ask_1;
  if (fresh_ask <= 0.0 && st->book_guard.last_valid_ask_1 > 0.0) {
    fresh_ask = st->book_guard.last_valid_ask_1;
  }
  if (!HkShortAskInOriginalBand(fresh_ask, st->hk_short_band_lo, st->hk_short_band_hi)) {
    st->entry_blocked = true;
    st->trading_fatal_code = "HK_SHORT_ENTRY_UNFILLED";
    st->trading_fatal_detail = HkShortPriceOutOfBandDetail(
        fresh_ask, st->hk_short_band_lo, st->hk_short_band_hi, st->hk_short_band_ref_ask);
    return true;
  }
  const double qty = st->pending.target_qty;
  const std::string side = st->pending.side;
  st->pending = PendingOrderState();
  st->entry_await_fill = false;
  st->hk_short_place_attempt = attempt + 1;
  const double submit_px = EmergencyLimitSubmitPx(st->sym.code, side, fresh_ask);
  PlaceAttemptResult pr =
      PlacePendingOrder(st, client, PendingKind::kEntry, side, qty, submit_px, "NORMAL", false, false,
                    fresh_ask, store, trd_env, "");
  if (pr.ok) {
    st->pending.placed_mono = now_mono;
    if (store != NULL) {
      store->LogEvent("INFO", "hk_short_reprice", st->sym.code + " attempt=" +
                                      std::to_string(st->hk_short_place_attempt));
    }
    return false;
  }
  if (IsWaitableSymbolNotTradableFail(pr.error)) {
    ScheduleNotTradableWait(st, pr.error, now_mono, store);
  }
  return false;
}

}  // namespace

namespace {

void LatencyOnceLegTs(SymbolLoopState::LegLatencyOnce *flags, const char *field,
                      bool SymbolLoopState::LegLatencyOnce::*member) {
  if (flags == NULL || field == NULL || flags->*member) {
    return;
  }
  mmtrade::task::TaskLatencyRecorder &rec = mmtrade::task::TaskLatencyRecorder::Instance();
  if (!rec.Enabled()) {
    return;
  }
  flags->*member = true;
  rec.RecordTimestampMs(field);
}

SymbolLoopState::LegLatencyOnce *LegLatencyFlags(SymbolLoopState *st, PendingKind kind) {
  if (st == NULL) {
    return NULL;
  }
  return kind == PendingKind::kEntry ? &st->entry_latency : &st->exit_latency;
}

const char *PlaceApiField(PendingKind kind) {
  return kind == PendingKind::kEntry ? "entry_place_api_ms" : "exit_place_api_ms";
}

const char *PlaceSentField(PendingKind kind) {
  return kind == PendingKind::kEntry ? "entry_place_sent_ms" : "exit_place_sent_ms";
}

const char *PlacePreUsField(PendingKind kind) {
  return kind == PendingKind::kEntry ? "entry_place_pre_us" : "exit_place_pre_us";
}

const char *PlaceSdkUsField(PendingKind kind) {
  return kind == PendingKind::kEntry ? "entry_place_sdk_us" : "exit_place_sdk_us";
}

const char *PlacePostUsField(PendingKind kind) {
  return kind == PendingKind::kEntry ? "entry_place_post_us" : "exit_place_post_us";
}

const char *PushFirstField(PendingKind kind) {
  return kind == PendingKind::kEntry ? "entry_push_first_ms" : "exit_push_first_ms";
}

const char *PollFirstField(PendingKind kind) {
  return kind == PendingKind::kEntry ? "entry_poll_first_ms" : "exit_poll_first_ms";
}

const char *PushSubmittingField(PendingKind kind) {
  return kind == PendingKind::kEntry ? "entry_push_submitting_ms" : "exit_push_submitting_ms";
}

const char *PushFilledField(PendingKind kind) {
  return kind == PendingKind::kEntry ? "entry_push_filled_ms" : "exit_push_filled_ms";
}

const char *FillSourceField(PendingKind kind) {
  return kind == PendingKind::kEntry ? "entry_fill_source" : "exit_fill_source";
}

const char *ApplyFillField(PendingKind kind) {
  return kind == PendingKind::kEntry ? "entry_apply_fill_ms" : "exit_apply_fill_ms";
}

void RecordPlaceBreakdown(PendingKind kind, const mmtrade::opend::PlaceOrderResult &pr,
                          int64_t api_ms) {
  mmtrade::task::TaskLatencyRecorder &rec = mmtrade::task::TaskLatencyRecorder::Instance();
  if (!rec.Enabled()) {
    return;
  }
  rec.RecordValue(PlaceApiField(kind), api_ms);
  if (pr.pre_sdk_us > 0) {
    rec.RecordValue(PlacePreUsField(kind), pr.pre_sdk_us);
  }
  if (pr.sdk_us > 0) {
    rec.RecordValue(PlaceSdkUsField(kind), pr.sdk_us);
  }
  if (pr.post_sdk_us > 0) {
    rec.RecordValue(PlacePostUsField(kind), pr.post_sdk_us);
  }
  const bool entry = (kind == PendingKind::kEntry);
  if (pr.gateway_queue_wait_us > 0) {
    rec.RecordValue(entry ? "entry_gateway_queue_wait_us" : "exit_gateway_queue_wait_us",
                    pr.gateway_queue_wait_us);
  }
  if (pr.gateway_rate_wait_us > 0) {
    rec.RecordValue(entry ? "entry_gateway_rate_wait_us" : "exit_gateway_rate_wait_us",
                    pr.gateway_rate_wait_us);
  }
  if (pr.gateway_http_rtt_us > 0) {
    rec.RecordValue(entry ? "entry_gateway_http_rtt_us" : "exit_gateway_http_rtt_us",
                    pr.gateway_http_rtt_us);
  }
  if (!pr.place_via.empty()) {
    rec.RecordString(entry ? "entry_place_via" : "exit_place_via", pr.place_via);
  }
}

void RecordFillResolveLatency(SymbolLoopState *st, PendingKind kind,
                              const FillResolveResult &resolved, double target_qty) {
  if (st == NULL || !resolved.has_row) {
    return;
  }
  mmtrade::task::TaskLatencyRecorder &rec = mmtrade::task::TaskLatencyRecorder::Instance();
  if (!rec.Enabled()) {
    return;
  }
  SymbolLoopState::LegLatencyOnce *flags = LegLatencyFlags(st, kind);
  if (flags == NULL) {
    return;
  }
  if (!flags->fill_source) {
    flags->fill_source = true;
    rec.RecordString(FillSourceField(kind), resolved.source);
  }
  if (resolved.source == "push") {
    if (!flags->push_first) {
      flags->push_first = true;
      rec.RecordTimestampMs(PushFirstField(kind));
    }
    if (IsOrderPushSubmitting(resolved.row.order_status) && !flags->push_submitting) {
      flags->push_submitting = true;
      rec.RecordTimestampMs(PushSubmittingField(kind));
    }
    if (IsOrderPushFilled(resolved.row.order_status, resolved.row.dealt_qty, target_qty) &&
        !flags->push_filled) {
      flags->push_filled = true;
      rec.RecordTimestampMs(PushFilledField(kind));
    }
  } else if (resolved.source == "poll") {
    if (!flags->poll_first) {
      flags->poll_first = true;
      rec.RecordTimestampMs(PollFirstField(kind));
    }
  }
}

}  // namespace

PlaceAttemptResult PlacePendingOrder(SymbolLoopState *st, mmtrade::opend::OrderClient *client,
                                     PendingKind kind, const std::string &side, double qty,
                                     double price, const std::string &order_type,
                                     bool book_guard_emergency, bool book_guard_emergency_limit,
                                     double ref_px, log::StrategyStore *store,
                                     const std::string &trd_env, const std::string &exit_rule_id,
                                     const std::string &remark) {
  PlaceAttemptResult out;
  const std::string use_remark =
      remark.empty() ? (kind == PendingKind::kEntry ? "strategy_cpp_entry" : "strategy_cpp_exit")
                     : remark;
  const double log_ref_px = ref_px > 0.0 ? ref_px : price;
  if (store != NULL) {
    log::LogOrderRequest(store, st->sym, side, qty, price, log_ref_px, trd_env, use_remark);
  }
  const int64_t t_place0 = mmtrade::task::TaskLatencyRecorder::WallMsNow();
  const std::string order_sess_raw =
      st->sym.order_session.empty() ? "RTH" : st->sym.order_session;
  const bool is_paper = (trd_env == "paper" || trd_env == "SIMULATE");
  const bool fill_outside = FillOutsideRthForSession(order_sess_raw, is_paper);
  const std::string sdk_sess = SdkSessionStringForPlace(order_sess_raw, is_paper);
  const std::string order_sess_place =
      mmtrade::strategy::OrderSessionForPlace(order_sess_raw, is_paper);
  if (is_paper && mmtrade::strategy::NormalizeOrderSession(order_sess_raw) == "ALL" &&
      order_sess_place == "RTH" && store != NULL) {
    store->LogEvent("INFO", "order_session_paper_downgrade",
                    "ALL→RTH for paper PlaceOrder code=" + st->sym.code);
  }
  const std::string order_sess = order_sess_place;
  const std::string tif = st->sym.time_in_force.empty() ? "DAY" : st->sym.time_in_force;
  const std::string ot_use =
      ClampOrderTypeForSession(order_sess_raw, order_type, st->last_market_phase);
  const mmtrade::opend::PlaceOrderResult pr =
      client->Place(st->sym.code, side, qty, price, ot_use, use_remark, tif, fill_outside,
                    sdk_sess);
  if (mmtrade::task::TaskLatencyRecorder::Instance().Enabled()) {
    const int64_t api_ms = mmtrade::task::TaskLatencyRecorder::WallMsNow() - t_place0;
    RecordPlaceBreakdown(kind, pr, api_ms);
  }
  if (!pr.ok || (pr.order_id == 0 && pr.order_id_ex.empty())) {
    out.error = pr.error;
    if (store != NULL) {
      log::LogPlaceOrderResult(store, false, "", "", pr.error);
      store->LogEvent("WARN", "place_failed", pr.error);
    }
    return out;
  }
  st->pending = PendingOrderState();
  st->pending.kind = kind;
  st->pending.order_id = pr.order_id;
  st->pending.order_id_ex = pr.order_id_ex;
  st->pending.target_qty = qty;
  st->pending.submit_price = price;
  st->pending.ref_px = ref_px > 0.0 ? ref_px : price;
  st->pending.side = side;
  st->pending.exit_rule_id = exit_rule_id;
  st->pending.order_type_used = order_type;
  st->pending.book_guard_emergency = book_guard_emergency;
  st->pending.book_guard_emergency_limit = book_guard_emergency_limit;
  st->pending.hk_short_place_attempt = st->hk_short_place_attempt;
  st->pending.hk_short_band_lo = st->hk_short_band_lo;
  st->pending.hk_short_band_hi = st->hk_short_band_hi;
  st->pending.hk_short_band_ref_ask = st->hk_short_band_ref_ask;
  if (kind == PendingKind::kEntry) {
    st->entry_await_fill = true;
  } else {
    st->exit_await_fill = true;
  }
  if (store != NULL) {
    const std::string oid_str =
        trade::ResolveDisplayOrderId(pr.order_id, pr.order_id_ex);
    store->SaveOrder(st->sym.task_id, st->sym.code, side, oid_str, "SUBMITTED", qty, price,
                     use_remark, "", "", "", trd_env);
    log::LogPlaceOrderResult(store, true, oid_str, "SUBMITTING", "");
    log::LogAwaitFill(store, oid_str, kind == PendingKind::kEntry);
  }
  out.ok = true;
  if (mmtrade::task::TaskLatencyRecorder::Instance().Enabled()) {
    mmtrade::task::TaskLatencyRecorder::Instance().RecordTimestampMs(PlaceSentField(kind));
    if (kind == PendingKind::kEntry) {
      mmtrade::task::PipelineLatency::OnEntryOrderSent();
      mmtrade::task::PipelineLatency::OnEntryOrderAck();
    } else {
      mmtrade::task::PipelineLatency::OnExitOrderSent();
      mmtrade::task::PipelineLatency::OnExitOrderAck();
    }
  }
  return out;
}

void ApplyEntryFill(SymbolLoopState *st, double fill_qty, double fill_px, int lot_size) {
  if (st == NULL) {
    return;
  }
  const double prev_opened = st->sym.opened_qty;
  const double prev_price = st->sym.opened_price;
  st->sym.opened_qty += fill_qty;
  if (st->sym.opened_price <= 0.0) {
    st->sym.opened_price = fill_px;
  } else if (prev_opened > 0.0) {
    st->sym.opened_price =
        (prev_opened * prev_price + fill_qty * fill_px) / (prev_opened + fill_qty);
  }
  st->sym.status =
      StatusAfterEntryFillFull(st->sym, prev_opened, prev_price, fill_qty, fill_px, lot_size);
  if (st->sym.status >= 2) {
    st->entry_done = true;
  }
  st->hk_short_place_attempt = 0;
  st->hk_short_band_ref_ask = 0.0;
  st->hk_short_band_lo = 0.0;
  st->hk_short_band_hi = 0.0;
  st->entry_retry_failed_count = 0;
  st->entry_force_market_next = false;
  ResetBookGuardEmergencyState(&st->book_guard);
}

void ApplyExitFill(SymbolLoopState *st, double fill_qty, double fill_px) {
  if (st == NULL) {
    return;
  }
  st->sym.closed_qty += fill_qty;
  if (st->sym.closed_price <= 0.0) {
    st->sym.closed_price = fill_px;
  }
  if (ExitCycleComplete(st->sym)) {
    st->sym.status = 3;
    st->exit_done = true;
  } else {
    st->sym.status = 2;
  }
  st->exit_retry_failed_count = 0;
  st->exit_force_market_next = false;
  ResetBookGuardEmergencyState(&st->book_guard);
}

bool TryPlaceEntry(SymbolLoopState *st, const config::StrategyConfig &cfg,
                   mmtrade::opend::OrderClient *client, const BookTick &book,
                   const opend::QuoteSnapshot &snap, double now_ts, double now_mono,
                   log::StrategyStore *store, const std::string &trd_env) {
  if (st == NULL || client == NULL || st->entry_await_fill || IsAwaitingFill(*st) ||
      st->entry_blocked) {
    return false;
  }
  if (now_mono < st->place_cooldown_until_mono) {
    return false;
  }
  RefreshSymbolTradability(st, snap, now_ts, store);
  if (!st->symbol_tradable) {
    return false;
  }
  if (st->sym.status != 0 && st->sym.status != 1) {
    return false;
  }
  if (st->entry_done) {
    return false;
  }
  const bool hk_short =
      IsHkShortOpenEntry(st->sym.code, st->sym.entry_side, st->sym.entry_side);
  if (!HkShortEntryBandOk(st, book, now_ts, store)) {
    return false;
  }
  const RefPriceResult ref = ResolveRefPriceWithBookGuard(
      st->sym.entry_side, book.bid_1, book.ask_1, &st->book_guard, now_ts, hk_short);
  if (!ref.ok) {
    std::string note;
    const double qty = ResolveEntryQtyForPlace(st->sym, snap.last_price > 0 ? snap.last_price : 0.01,
                                             st->lot_size, &note);
    if (qty <= 0.0) {
      return false;
    }
    return TryBookGuardEmergencyEntry(st, client, book, snap, now_mono, qty, store, trd_env);
  }
  std::string note;
  const double px = ref.price > 0.0 ? ref.price : (book.ask_1 > 0 ? book.ask_1 : book.bid_1);
  const double qty = ResolveEntryQtyForPlace(st->sym, px, st->lot_size, &note);
  if (qty <= 0.0) {
    return false;
  }

  mmtrade::task::PipelineLatency::OnEntryRiskPass();

  EntryNetSplitParams net_params;
  net_params.state = st;
  net_params.store = store;
  net_params.client = client;
  net_params.cfg = &cfg;
  net_params.trd_env = trd_env;
  net_params.book = &book;
  net_params.px = px;
  net_params.qty = qty;
  net_params.remark = "strategy_cpp_entry";
  net_params.book_guard_emergency = false;
  net_params.in_rth = true;
  if (TryExecuteEntryNetSplit(net_params)) {
    return true;
  }

  const OrderTypeRetryChoice ot_choice = ResolveEntryOrderTypeForRetry(st, cfg);
  double place_px = 0.0;
  const int64_t t_px0 = mmtrade::task::TaskLatencyRecorder::WallUsNow();
  if (!ot_choice.market_fallback) {
    place_px = hk_short ? EmergencyLimitSubmitPx(st->sym.code, st->sym.entry_side, px)
                        : ComputeSubmitPriceForAttempt(st->sym, book, st->sym.entry_side,
                                                      st->entry_retry_failed_count, false);
  }
  if (mmtrade::task::TaskLatencyRecorder::Instance().Enabled()) {
    const int64_t px_us = mmtrade::task::TaskLatencyRecorder::WallUsNow() - t_px0;
    mmtrade::task::TaskLatencyRecorder::Instance().RecordValue("entry_price_compute_us", px_us);
    LatencyOnceLegTs(&st->entry_latency, "entry_place_start_ms",
                     &SymbolLoopState::LegLatencyOnce::place_start);
  }
  if (store != NULL) {
    log::LogOrderSizing(store, st->sym, px, qty, st->lot_size, note);
  }
  PlaceAttemptResult pr =
      PlacePendingOrder(st, client, PendingKind::kEntry, st->sym.entry_side, qty, place_px,
                        ot_choice.order_type, false, false, px, store, trd_env, "");
  if (!pr.ok) {
    (void)BumpEntryOrderFail(st, cfg, now_mono, pr.error, "", "", store);
    return false;
  }
  st->pending.placed_mono = now_mono;
  const std::string hit_label =
      log::FormatEntryRuleHitLabel(st->sym, st->last_entry_hit_rule_id);
  std::ostringstream entry_console;
  entry_console << "[" << st->sym.code << "] 开仓触发 entry_hit=" << hit_label;
  if (!st->last_entry_hit_rule_id.empty()) {
    entry_console << " rule=" << st->last_entry_hit_rule_id;
  }
  entry_console << " | 下单 entry oid=" << PendingDisplayOrderId(st->pending)
                << " px=" << st->pending.submit_price
                << " qty=" << static_cast<long long>(qty);
  log::LogConsoleInfo(entry_console.str());
  return true;
}

bool TryPlaceExit(SymbolLoopState *st, const config::StrategyConfig &cfg,
                  mmtrade::opend::OrderClient *client, const BookTick &book,
                  const opend::QuoteSnapshot &snap, double now_ts, double now_mono,
                  const std::string &exit_rule_id, log::StrategyStore *store,
                  const std::string &trd_env) {
  if (st == NULL || client == NULL || st->exit_await_fill || IsAwaitingFill(*st) ||
      st->entry_blocked) {
    return false;
  }
  if (now_mono < st->place_cooldown_until_mono) {
    return false;
  }
  RefreshSymbolTradability(st, snap, now_ts, store);
  if (!st->symbol_tradable) {
    return false;
  }
  if (st->sym.status != 2 || !st->entry_done || st->exit_done) {
    return false;
  }
  const std::string exit_side = ResolveExitSide(st->sym, exit_rule_id);
  const RefPriceResult ref =
      ResolveRefPriceWithBookGuard(exit_side, book.bid_1, book.ask_1, &st->book_guard, now_ts, false);
  const double qty = ResolveExitQty(st->sym);
  // Reject sub-share / garbage qty before Place (broker → "Invalid quantity.").
  if (qty < 1.0) {
    if (store != NULL) {
      store->LogEvent("WARN", "exit_qty_invalid",
                      "ResolveExitQty=" + std::to_string(qty) + " opened=" +
                          std::to_string(st->sym.opened_qty) + " closed=" +
                          std::to_string(st->sym.closed_qty) + " exit_qty=" +
                          std::to_string(st->sym.exit_qty));
    }
    return false;
  }
  if (!ref.ok) {
    return TryBookGuardEmergencyExit(st, client, book, snap, now_mono, exit_side, qty, exit_rule_id,
                                   store, trd_env);
  }

  mmtrade::task::PipelineLatency::OnExitRiskPass();

  ExitNetSplitParams net_params;
  net_params.state = st;
  net_params.store = store;
  net_params.client = client;
  net_params.cfg = &cfg;
  net_params.trd_env = trd_env;
  net_params.book = &book;
  net_params.px = ref.price;
  net_params.exit_slice = qty;
  net_params.remark = "strategy_cpp_exit";
  net_params.exit_rule_id = exit_rule_id;
  net_params.book_guard_emergency = false;
  net_params.in_rth = true;
  if (TryExecuteExitNetSplit(net_params)) {
    return true;
  }

  const OrderTypeRetryChoice ot_choice = ResolveExitOrderTypeForRetry(st, cfg);
  const double submit_px =
      ot_choice.market_fallback
          ? 0.0
          : ComputeSubmitPriceForAttempt(st->sym, book, exit_side, st->exit_retry_failed_count,
                                         ot_choice.market_fallback);
  if (mmtrade::task::TaskLatencyRecorder::Instance().Enabled()) {
    LatencyOnceLegTs(&st->exit_latency, "exit_place_start_ms",
                     &SymbolLoopState::LegLatencyOnce::place_start);
  }
  PlaceAttemptResult pr =
      PlacePendingOrder(st, client, PendingKind::kExit, exit_side, qty, submit_px,
                        ot_choice.order_type, false, false, ref.price, store, trd_env,
                        exit_rule_id);
  if (!pr.ok) {
    (void)BumpExitOrderFail(st, cfg, now_mono, pr.error, "", "", store);
    return false;
  }
  st->pending.placed_mono = now_mono;
  const std::string hit_label = log::FormatExitRuleHitLabel(st->sym, exit_rule_id);
  if (store != NULL) {
    log::LogStartExit(store, st->sym.code, hit_label);
  }
  std::ostringstream exit_console;
  exit_console << "[" << st->sym.code << "] 平仓触发 exit_hit=" << hit_label;
  if (!exit_rule_id.empty()) {
    exit_console << " rule=" << exit_rule_id;
  }
  exit_console << " | 下单 exit oid=" << PendingDisplayOrderId(st->pending)
               << " px=" << st->pending.submit_price
               << " qty=" << static_cast<long long>(qty);
  log::LogConsoleInfo(exit_console.str());
  if (store != NULL) {
    store->LogEvent("INFO", "place_exit", "oid=" + PendingDisplayOrderId(st->pending));
  }
  return true;
}

PendingAdvanceResult AdvancePendingOrder(SymbolLoopState *st, const config::StrategyConfig &cfg,
                                         mmtrade::opend::OrderClient *client,
                                         const BookTick &book, const opend::QuoteSnapshot &snap,
                                         double now_ts, double now_mono,
                                         const FillHooks *hooks) {
  if (st == NULL || client == NULL || !IsAwaitingFill(*st)) {
    return PendingAdvanceResult::kIdle;
  }
  if (st->entry_blocked) {
    return PendingAdvanceResult::kFatal;
  }
  RefreshSymbolTradability(st, snap, now_ts, hooks != NULL ? hooks->store : NULL);
  const PendingKind kind = st->pending.kind;
  const std::string oid = PendingDisplayOrderId(st->pending);
  const double poll_min = cfg.order_poll_min_sec > 0.0 ? cfg.order_poll_min_sec : 3.0;
  const FillResolveResult resolved = ResolveOrderRowForFill(
      client->PushHub(), oid, st->pending.placed_mono, now_mono, OrderFillPushSilenceSec(),
      poll_min,
      &st->pending.last_poll_mono, [&client, st]() {
        return client->QueryOrder(st->pending.order_id, st->sym.code, st->pending.order_id_ex);
      });
  if (!resolved.has_row) {
    return PendingAdvanceResult::kWaiting;
  }
  RecordFillResolveLatency(st, kind, resolved, st->pending.target_qty);
  if (resolved.has_row) {
    EntryBookGuardEmergencyStaleToMarket(st, client, resolved, now_mono,
                                         hooks != NULL ? hooks->store : NULL,
                                         hooks != NULL ? hooks->trd_env : "");
    ExitBookGuardEmergencyStaleToMarket(st, client, resolved, now_mono,
                                          hooks != NULL ? hooks->store : NULL,
                                          hooks != NULL ? hooks->trd_env : "");
    if (kind == PendingKind::kEntry &&
        EntryHkShortUnfilledReplace(st, client, cfg, book, resolved, now_mono,
                                    hooks != NULL ? hooks->store : NULL,
                                    hooks != NULL ? hooks->trd_env : "")) {
      return PendingAdvanceResult::kFatal;
    }
  }
  const FillClassification fill = ClassifyOrderFill(resolved.row, st->pending.target_qty);
  if (hooks != NULL && hooks->store != NULL) {
    std::string outcome = "wait";
    if (fill.outcome == FillOutcome::kFilled) {
      outcome = "filled";
    } else if (fill.outcome == FillOutcome::kFailed) {
      outcome = "failed";
    }
    log::LogOrderPoll(hooks->store, st->sym.code, oid, resolved.row.order_status, fill.dealt_qty,
                      st->pending.target_qty, kind == PendingKind::kExit, outcome);
  }
  if (fill.outcome == FillOutcome::kWait) {
    const double stale_sec =
        cfg.order_unfilled_replace_sec > 0.0 ? cfg.order_unfilled_replace_sec : 12.0;
    const std::string ot = st->pending.order_type_used.empty() ? st->sym.order_type
                                                               : st->pending.order_type_used;
    if (!st->pending.book_guard_emergency_limit && stale_sec > 0.0 &&
        IsStaleReplaceOrderType(ot) && IsStaleWaitStatus(resolved.row.order_status) &&
        fill.dealt_qty + 1e-9 < st->pending.target_qty &&
        (now_mono - st->pending.placed_mono) >= stale_sec &&
        !IsHkShortOpenEntry(st->sym.code, st->sym.entry_side, st->sym.entry_side)) {
      const int retry_max = cfg.order_retry_max >= 0 ? cfg.order_retry_max : 3;
      int *fail_n = (kind == PendingKind::kEntry) ? &st->entry_retry_failed_count
                                                 : &st->exit_retry_failed_count;
      *fail_n += 1;
      if (*fail_n > retry_max) {
        client->CancelOrder(st->pending.order_id, st->sym.code, st->pending.order_id_ex);
        st->pending = PendingOrderState();
        if (kind == PendingKind::kEntry) {
          st->entry_await_fill = false;
        } else {
          st->exit_await_fill = false;
        }
        std::ostringstream line;
        line << "连续重试报单失败/"
             << (kind == PendingKind::kEntry ? "entry" : "exit")
             << "_retry_exceeded_stop | code=" << st->sym.code << " retry_max=" << retry_max
             << " 连续" << (retry_max + 1) << "次仍未成交 => stop";
        SetTradingFatal(st,
                        RetryExceededFatalCode(
                            st->sym.order_session.empty() ? "RTH" : st->sym.order_session,
                            st->last_market_phase,
                            kind == PendingKind::kEntry ? "entry" : "exit"),
                        "stale_unfilled retry_max=" + std::to_string(retry_max),
                        hooks != NULL ? hooks->store : NULL,
                        kind == PendingKind::kEntry ? "entry_retry_exceeded_stop"
                                                    : "exit_retry_exceeded_stop",
                        line.str());
        return PendingAdvanceResult::kFatal;
      }
      client->CancelOrder(st->pending.order_id, st->sym.code, st->pending.order_id_ex);
      if (mmtrade::task::TaskLatencyRecorder::Instance().Enabled()) {
        if (kind == PendingKind::kEntry) {
          mmtrade::task::TaskLatencyRecorder::Instance().Increment("entry_cancel_count");
        } else {
          mmtrade::task::TaskLatencyRecorder::Instance().Increment("exit_cancel_count");
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(
          static_cast<int>(cfg.order_cancel_retry_wait_sec * 1000.0)));
      const bool market_fallback =
          *fail_n >= retry_max &&
          SessionAllowsMarketOrder(st->sym.order_session.empty() ? "RTH" : st->sym.order_session,
                                   st->last_market_phase);
      const int attempt = st->pending.replace_count + 1;
      const double qty = st->pending.target_qty;
      const std::string side = st->pending.side;
      const std::string exit_rule = st->pending.exit_rule_id;
      st->pending = PendingOrderState();
      if (kind == PendingKind::kEntry) {
        st->entry_await_fill = false;
      } else {
        st->exit_await_fill = false;
      }
      const double price =
          market_fallback ? 0.0 : ComputeSubmitPriceForAttempt(st->sym, book, side, attempt, false);
      const std::string order_type = market_fallback ? "MARKET" : st->sym.order_type;
      PlaceAttemptResult pr =
          PlacePendingOrder(st, client, kind, side, qty, price, order_type, false, false, price,
                            hooks != NULL ? hooks->store : NULL,
                            hooks != NULL ? hooks->trd_env : "", exit_rule);
      if (pr.ok) {
        st->pending.placed_mono = now_mono;
        st->pending.replace_count = attempt;
        if (mmtrade::task::TaskLatencyRecorder::Instance().Enabled()) {
          if (kind == PendingKind::kEntry) {
            mmtrade::task::TaskLatencyRecorder::Instance().Increment("entry_replace_count");
          } else {
            mmtrade::task::TaskLatencyRecorder::Instance().Increment("exit_replace_count");
          }
        }
        if (hooks != NULL && hooks->store != NULL) {
          hooks->store->LogEvent("INFO", "stale_unfilled_replace", "oid=" + oid);
        }
        return PendingAdvanceResult::kReplaced;
      }
      if (IsWaitableSymbolNotTradableFail(pr.error)) {
        ScheduleNotTradableWait(st, pr.error, now_mono, hooks != NULL ? hooks->store : NULL);
      }
      st->place_cooldown_until_mono = now_mono + cfg.order_cancel_retry_wait_sec;
    }
    return PendingAdvanceResult::kWaiting;
  }

  const double submit_px = st->pending.submit_price;
  if (fill.outcome == FillOutcome::kFailed) {
    if (kind == PendingKind::kEntry) {
      mmtrade::task::PipelineLatency::OnEntryOrderTerminal("failed");
    } else {
      mmtrade::task::PipelineLatency::OnExitOrderTerminal("failed");
    }
    const std::string fail_status = resolved.row.order_status;
    const std::string fail_detail = fail_status;
    const PendingKind pk = st->pending.kind;
    st->pending = PendingOrderState();
    OrderFailBumpResult bump = OrderFailBumpResult::kContinue;
    if (pk == PendingKind::kEntry) {
      ResetBookGuardEmergencyState(&st->book_guard);
      bump = BumpEntryOrderFail(st, cfg, now_mono, fail_detail, oid, fail_status,
                               hooks != NULL ? hooks->store : NULL);
    } else {
      bump = BumpExitOrderFail(st, cfg, now_mono, fail_detail, oid, fail_status,
                              hooks != NULL ? hooks->store : NULL);
    }
    if (bump == OrderFailBumpResult::kFatal) {
      return PendingAdvanceResult::kFatal;
    }
    if (hooks != NULL && hooks->store != NULL) {
      hooks->store->LogEvent("WARN", "fill_failed", fail_status);
    }
    return PendingAdvanceResult::kFailed;
  }

  if (kind == PendingKind::kEntry) {
    if (mmtrade::task::TaskLatencyRecorder::Instance().Enabled()) {
      mmtrade::task::TaskLatencyRecorder::Instance().RecordTimestampMs("entry_filled_ms");
    }
    mmtrade::task::PipelineLatency::OnEntryOrderTerminal("fill");
    ApplyEntryFill(st, fill.dealt_qty, fill.dealt_avg_price, st->lot_size);
    st->entry_await_fill = false;
    if (mmtrade::task::TaskLatencyRecorder::Instance().Enabled()) {
      mmtrade::task::TaskLatencyRecorder::Instance().RecordTimestampMs("entry_apply_fill_ms");
    }
    log::LogConsoleInfo("[" + st->sym.code + "] 开仓成交 qty=" +
                        std::to_string(static_cast<long long>(fill.dealt_qty)) + " px=" +
                        std::to_string(fill.dealt_avg_price) + " entry_hit=" +
                        log::FormatEntryRuleHitLabel(st->sym, st->last_entry_hit_rule_id));
    if (hooks != NULL && hooks->store != NULL && hooks->cfg != NULL) {
      log::LogEntryFillBlock(hooks->store, *hooks->cfg, st->sym, fill.dealt_qty,
                             fill.dealt_avg_price, submit_px, oid, hooks->trd_env,
                             st->last_entry_hit_rule_id);
    }
  } else {
    if (mmtrade::task::TaskLatencyRecorder::Instance().Enabled()) {
      mmtrade::task::TaskLatencyRecorder::Instance().RecordTimestampMs("exit_filled_ms");
    }
    mmtrade::task::PipelineLatency::OnExitOrderTerminal("fill");
    ApplyExitFill(st, fill.dealt_qty, fill.dealt_avg_price);
    st->exit_await_fill = false;
    if (mmtrade::task::TaskLatencyRecorder::Instance().Enabled()) {
      mmtrade::task::TaskLatencyRecorder::Instance().RecordTimestampMs("exit_apply_fill_ms");
    }
    log::LogConsoleInfo("[" + st->sym.code + "] 平仓成交 qty=" +
                        std::to_string(static_cast<long long>(fill.dealt_qty)) + " px=" +
                        std::to_string(fill.dealt_avg_price) + " exit_hit=" +
                        log::FormatExitRuleHitLabel(st->sym, st->pending.exit_rule_id));
    if (hooks != NULL && hooks->store != NULL && hooks->cfg != NULL) {
      log::LogExitFillBlock(hooks->store, *hooks->cfg, st->sym, fill.dealt_qty,
                            fill.dealt_avg_price, submit_px, oid, hooks->trd_env,
                            st->pending.exit_rule_id);
    }
  }
  UpdateOrderOnFill(hooks, st, kind, oid, resolved.row.order_status, fill.dealt_qty,
                    fill.dealt_avg_price);
  NotifyFill(hooks, st, kind, oid, fill.dealt_qty, fill.dealt_avg_price, submit_px);
  if (hooks != NULL && !hooks->config_path.empty()) {
    PersistRuntimeQuiet(hooks->config_path, st->sym, hooks != NULL ? hooks->store : NULL);
  }
  st->pending = PendingOrderState();
  return PendingAdvanceResult::kFilled;
}

}  // namespace strategy
}  // namespace mmtrade
