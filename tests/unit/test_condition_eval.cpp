#include "config/condition_bridge.hpp"
#include "engine/compare.hpp"
#include "engine/eval/before_session_close_eval.hpp"
#include "engine/eval/day_open_gain_eval.hpp"
#include "engine/eval/kline_candle_eval.hpp"
#include "engine/eval/orderbook_eval.hpp"
#include "engine/eval/pre_entry_bars_low_stop_eval.hpp"
#include "engine/eval/trailing_ma20_close_eval.hpp"
#include "engine/eval/trailing_ma20_realtime_eval.hpp"
#include "engine/eval/trailing_take_profit_eval.hpp"
#include "engine/eval/volume_eval.hpp"
#include "engine/kline_candle_util.hpp"
#include "engine/registry.hpp"
#include "engine/session_close_util.hpp"
#include "engine/staged_take_profit_util.hpp"

#include <gtest/gtest.h>

using mmtrade::config::FillEntryEvalBundle;
using mmtrade::config::FillExitEvalBundle;
using mmtrade::config::ConditionKind;
using mmtrade::engine::AllocateTrancheQtys;
using mmtrade::engine::CompareFloat;
using mmtrade::engine::EntryEvalBundle;
using mmtrade::engine::EvaluateEntryCondition;
using mmtrade::engine::EvaluateExitCondition;
using mmtrade::engine::EvaluateKlineCandle;
using mmtrade::engine::EvaluateKlineCandleShape;
using mmtrade::engine::EvaluateKlineClose;
using mmtrade::engine::EvaluateOrderbook;
using mmtrade::engine::EvaluatePnl;
using mmtrade::engine::BeforeSessionCloseEvalContext;
using mmtrade::engine::DayOpenGainCondition;
using mmtrade::engine::DayOpenGainEvalContext;
using mmtrade::engine::DayOpenGainRuntime;
using mmtrade::engine::EvaluateBeforeSessionClose;
using mmtrade::engine::EvaluateDayOpenGain;
using mmtrade::engine::EvaluatePreEntryBarsLowStop;
using mmtrade::engine::EvaluateStagedTakeProfit;
using mmtrade::engine::EvaluateTrailingMa20Close;
using mmtrade::engine::EvaluateTrailingMa20Realtime;
using mmtrade::engine::EvalResult;
using mmtrade::engine::ExitEvalBundle;
using mmtrade::engine::FieldMap;
using mmtrade::engine::KlineCandleCondition;
using mmtrade::engine::KlineCandleEvalContext;
using mmtrade::engine::KlineCandleFetchResult;
using mmtrade::engine::KlineCandleRuntime;
using mmtrade::engine::KlineCloseCondition;
using mmtrade::engine::KlineCloseEvalContext;
using mmtrade::engine::KlineCloseFetchResult;
using mmtrade::engine::KlineCloseRuntime;
using mmtrade::engine::NormalizeKlineCandleRef;
using mmtrade::engine::PnlCondition;
using mmtrade::engine::PnlEvalContext;
using mmtrade::engine::PriceCondition;
using mmtrade::engine::VolumeComputeResult;
using mmtrade::engine::VolumeEntryWhen;
using mmtrade::engine::VolumeEvalContext;
using mmtrade::engine::ResolveStagedBaselineQty;
using mmtrade::engine::StagedTakeProfitCondition;
using mmtrade::engine::StagedTpEvalContext;
using mmtrade::engine::StagedTpRuntime;
using mmtrade::engine::EvaluateTrailingTakeProfit;
using mmtrade::engine::EvaluateVolume;
using mmtrade::engine::LocalDateTime;
using mmtrade::engine::PreEntryBarsLowStopCondition;
using mmtrade::engine::PreEntryLowStopEvalContext;
using mmtrade::engine::PreEntryLowStopRuntime;
using mmtrade::engine::SessionCloseWindowRthLocal;
using mmtrade::engine::TrailingMa20CloseCondition;
using mmtrade::engine::TrailingMa20CloseEvalContext;
using mmtrade::engine::TrailingMa20CloseRuntime;
using mmtrade::engine::TrailingMa20RealtimeCondition;
using mmtrade::engine::TrailingMa20RtEvalContext;
using mmtrade::engine::TrailingMa20RtRuntime;
using mmtrade::engine::TrailingTakeProfitCondition;
using mmtrade::engine::BeforeSessionCloseCondition;
using mmtrade::engine::TrailingTpEvalContext;
using mmtrade::engine::TrailingTpRuntime;
using mmtrade::engine::VolumeRuntime;
using mmtrade::engine::kKlineCandleRefBarOpen;
using mmtrade::engine::kKlineCandleRefPrevClose;

// Mirrors condition_engine/evaluators/shared/orderbook.py

TEST(CompareFloat, SupportsAllOps) {
  EXPECT_TRUE(CompareFloat(1.0, "<", 2.0));
  EXPECT_TRUE(CompareFloat(2.0, "<=", 2.0));
  EXPECT_TRUE(CompareFloat(3.0, ">", 2.0));
  EXPECT_TRUE(CompareFloat(2.0, ">=", 2.0));
  EXPECT_TRUE(CompareFloat(2.0, "==", 2.0));
  EXPECT_TRUE(CompareFloat(2.0, "!=", 3.0));
}

TEST(CompareFloat, InvalidOpThrows) {
  EXPECT_THROW(CompareFloat(1.0, "~", 2.0), std::invalid_argument);
}

TEST(OrderbookEval, EmptyOpAlwaysHits) {
  PriceCondition cond;
  cond.field = "ask_1";
  cond.op = "";
  FieldMap fields;
  fields["ask_1"] = 150.0;
  const EvalResult out = EvaluateOrderbook(cond, fields);
  EXPECT_TRUE(out.handled);
  EXPECT_TRUE(out.hit);
  EXPECT_EQ(out.reason, "op_empty_skip_compare");
}

TEST(OrderbookEval, MissingFieldOrThreshold) {
  PriceCondition cond;
  cond.field = "ask_1";
  cond.op = ">";
  cond.has_value = true;
  cond.value = 100.0;
  const EvalResult out = EvaluateOrderbook(cond, FieldMap());
  EXPECT_TRUE(out.handled);
  EXPECT_FALSE(out.hit);
  EXPECT_EQ(out.reason, "missing_lv_or_thresh");
}

TEST(OrderbookEval, Ask1GreaterThanThreshold) {
  PriceCondition cond;
  cond.field = "ask_1";
  cond.op = ">";
  cond.has_value = true;
  cond.value = 100.0;
  FieldMap fields;
  fields["ask_1"] = 150.0;
  fields["bid_1"] = 149.0;
  const EvalResult out = EvaluateOrderbook(cond, fields);
  EXPECT_TRUE(out.hit);
  EXPECT_EQ(out.reason, "compared");
  EXPECT_DOUBLE_EQ(out.left_value, 150.0);
  EXPECT_DOUBLE_EQ(out.right_value, 100.0);
}

TEST(OrderbookEval, Bid1LessThanThresholdMisses) {
  PriceCondition cond;
  cond.field = "bid_1";
  cond.op = "<";
  cond.has_value = true;
  cond.value = 140.0;
  FieldMap fields;
  fields["bid_1"] = 149.0;
  const EvalResult out = EvaluateOrderbook(cond, fields);
  EXPECT_FALSE(out.hit);
}

// Mirrors condition_engine/evaluators/exit/pnl.py

TEST(PnlEval, UnavailableGainFraction) {
  PnlCondition cond;
  cond.op = ">=";
  cond.value = 0.05;
  const EvalResult out = EvaluatePnl(cond, PnlEvalContext());
  EXPECT_TRUE(out.handled);
  EXPECT_FALSE(out.hit);
  EXPECT_EQ(out.reason, "pnl_unavailable");
}

TEST(PnlEval, CompareGainFraction) {
  PnlCondition cond;
  cond.op = ">=";
  cond.value = 0.05;
  PnlEvalContext ctx;
  ctx.has_gain_fraction = true;
  ctx.gain_fraction = 0.1;
  const EvalResult out = EvaluatePnl(cond, ctx);
  EXPECT_TRUE(out.hit);
  EXPECT_EQ(out.reason, "compared");
  EXPECT_DOUBLE_EQ(out.left_value, 0.1);
}

// Mirrors condition_engine/evaluators/entry/kline_close.py (injected deps)

TEST(KlineCloseEval, MissingDependency) {
  KlineCloseCondition cond;
  const EvalResult out = EvaluateKlineClose(cond, KlineCloseEvalContext());
  EXPECT_FALSE(out.handled);
  EXPECT_EQ(out.reason, "missing_dependency");
}

TEST(KlineCloseEval, GateBlocksEvenWithEmptyOp) {
  KlineCloseRuntime rt;
  KlineCloseEvalContext ctx;
  ctx.now = 100.0;
  ctx.runtime = &rt;
  ctx.has_fetch = true;
  ctx.fetch.ok = true;
  ctx.fetch.close = 200.0;
  ctx.fetch.time_key = "2026-08-10 10:00:00";
  ctx.has_gate = true;
  ctx.gate_ok = false;

  KlineCloseCondition cond;
  cond.op = "";
  cond.poll_sec = 30.0;
  const EvalResult out = EvaluateKlineClose(cond, ctx);
  EXPECT_TRUE(out.handled);
  EXPECT_FALSE(out.hit);
}

TEST(KlineCloseEval, EmptyOpAfterGatePasses) {
  KlineCloseRuntime rt;
  KlineCloseEvalContext ctx;
  ctx.now = 100.0;
  ctx.runtime = &rt;
  ctx.has_fetch = true;
  ctx.fetch.ok = true;
  ctx.fetch.close = 200.0;
  ctx.has_gate = true;
  ctx.gate_ok = true;

  KlineCloseCondition cond;
  cond.op = "";
  const EvalResult out = EvaluateKlineClose(cond, ctx);
  EXPECT_TRUE(out.hit);
  EXPECT_EQ(out.reason, "kline_close");
}

TEST(KlineCloseEval, CompareCloseAgainstThreshold) {
  KlineCloseRuntime rt;
  KlineCloseEvalContext ctx;
  ctx.now = 100.0;
  ctx.runtime = &rt;
  ctx.has_fetch = true;
  ctx.fetch.ok = true;
  ctx.fetch.close = 150.0;
  ctx.has_gate = true;
  ctx.gate_ok = true;

  KlineCloseCondition cond;
  cond.op = ">=";
  cond.has_value = true;
  cond.value = 140.0;
  const EvalResult out = EvaluateKlineClose(cond, ctx);
  EXPECT_TRUE(out.hit);
  EXPECT_DOUBLE_EQ(out.left_value, 150.0);
}

TEST(KlineCloseEval, UsesCachedSnapshotWithinPollWindow) {
  KlineCloseRuntime rt;
  rt.last_eval_ts = 90.0;
  rt.snap_close = 120.0;

  KlineCloseEvalContext ctx;
  ctx.now = 100.0;
  ctx.runtime = &rt;
  ctx.has_fetch = true;
  ctx.fetch.ok = true;
  ctx.fetch.close = 999.0;
  ctx.has_gate = true;
  ctx.gate_ok = true;

  KlineCloseCondition cond;
  cond.op = ">=";
  cond.has_value = true;
  cond.value = 115.0;
  cond.poll_sec = 30.0;
  const EvalResult out = EvaluateKlineClose(cond, ctx);
  EXPECT_TRUE(out.hit);
  EXPECT_DOUBLE_EQ(out.left_value, 120.0);
}

// Mirrors condition_engine/evaluators/entry/volume.py

TEST(VolumeEval, MissingDependency) {
  VolumeEntryWhen cond;
  const EvalResult out = EvaluateVolume(cond, VolumeEvalContext());
  EXPECT_FALSE(out.handled);
  EXPECT_EQ(out.reason, "missing_dependency");
}

TEST(VolumeEval, FreshComputeThenCachedHit) {
  VolumeRuntime rt;
  VolumeEvalContext ctx;
  ctx.now = 100.0;
  ctx.runtime = &rt;
  ctx.has_compute = true;
  ctx.compute.hit = true;
  ctx.compute.vol = 5000.0;
  ctx.compute.thresh = 3000.0;

  VolumeEntryWhen cond;
  cond.poll_sec = 30.0;
  const EvalResult fresh = EvaluateVolume(cond, ctx);
  EXPECT_TRUE(fresh.hit);
  EXPECT_DOUBLE_EQ(fresh.left_value, 5000.0);

  ctx.now = 110.0;
  ctx.compute.hit = false;
  ctx.compute.vol = 100.0;
  const EvalResult cached = EvaluateVolume(cond, ctx);
  EXPECT_TRUE(cached.hit);
  EXPECT_DOUBLE_EQ(cached.left_value, 5000.0);
}

// Mirrors strategy_types.normalize_kline_candle_ref + _kline_candle_shape_hit

TEST(KlineCandleUtil, NormalizeRef) {
  EXPECT_EQ(NormalizeKlineCandleRef("bar_open"), kKlineCandleRefBarOpen);
  EXPECT_EQ(NormalizeKlineCandleRef("prev"), kKlineCandleRefPrevClose);
}

TEST(KlineCandleUtil, BullishBarOpenRef) {
  KlineCandleCondition cond;
  cond.candle = 1;
  cond.candle_ref = "bar_open";
  const mmtrade::engine::KlineCandleShapeResult shape =
      EvaluateKlineCandleShape(cond, 105.0, 100.0, 90.0);
  EXPECT_TRUE(shape.hit);
  EXPECT_EQ(shape.shape_rule, "阳(close>bar_open)");
}

TEST(KlineCandleUtil, TrueBullNeedsFakeAndPrevClose) {
  KlineCandleCondition cond;
  cond.candle = 1;
  cond.candle_ref = "prev_close";
  EXPECT_FALSE(EvaluateKlineCandleShape(cond, 102.0, 100.0, 103.0).hit);
  EXPECT_TRUE(EvaluateKlineCandleShape(cond, 102.0, 100.0, 100.0).hit);
}

TEST(KlineCandleEval, GateAndShapeRequired) {
  KlineCandleRuntime rt;
  KlineCandleEvalContext ctx;
  ctx.now = 100.0;
  ctx.runtime = &rt;
  ctx.has_fetch = true;
  ctx.fetch.ok = true;
  ctx.fetch.open = 100.0;
  ctx.fetch.close = 105.0;
  ctx.fetch.time_key = "2026-08-10 10:00:00";
  ctx.has_gate = true;
  ctx.gate_ok = true;

  KlineCandleCondition cond;
  cond.candle = 1;
  cond.candle_ref = "bar_open";
  const EvalResult out = EvaluateKlineCandle(cond, ctx);
  EXPECT_TRUE(out.hit);
  EXPECT_EQ(out.reason, "kline_candle");
}

TEST(KlineCandleEval, OnceModeLocksResult) {
  KlineCandleRuntime rt;
  rt.snap_open = 100.0;
  rt.snap_close = 105.0;
  rt.snap_time_key = "tk1";
  rt.once_done["rule:0:K_15M:1:0:bar_open"] = true;
  rt.once_ok["rule:0:K_15M:1:0:bar_open"] = true;

  KlineCandleEvalContext ctx;
  ctx.rule_id = "rule";
  ctx.cond_idx = 0;
  ctx.now = 200.0;
  ctx.runtime = &rt;
  ctx.has_fetch = true;
  ctx.fetch.ok = true;
  ctx.has_gate = true;
  ctx.gate_ok = true;

  KlineCandleCondition cond;
  cond.kl_type = "K_15M";
  cond.candle = 1;
  cond.candle_ref = "bar_open";
  cond.eval_mode = "once";
  const EvalResult out = EvaluateKlineCandle(cond, ctx);
  EXPECT_TRUE(out.hit);
}

TEST(DayOpenGainEval, ComparesRefAgainstOpenThreshold) {
  DayOpenGainRuntime rt;
  rt.day_open = 100.0;
  DayOpenGainEvalContext ctx;
  ctx.runtime = &rt;
  ctx.has_bid_ask = true;
  ctx.bid_1 = 109.0;
  ctx.ask_1 = 111.0;

  DayOpenGainCondition cond;
  cond.gain_pct = 0.05;
  cond.ref = "mid";
  const EvalResult out = EvaluateDayOpenGain(cond, ctx);
  EXPECT_TRUE(out.handled);
  EXPECT_TRUE(out.hit);
  EXPECT_EQ(out.reason, "compared");
  EXPECT_DOUBLE_EQ(out.right_value, 105.0);
}

TEST(TrailingMa20CloseEval, AppliesMaRefRelax) {
  TrailingMa20CloseRuntime rt;
  TrailingMa20CloseEvalContext ctx;
  ctx.now = 100.0;
  ctx.runtime = &rt;
  ctx.side = "BUY";
  ctx.has_compute = true;
  ctx.compute.ok = true;
  ctx.compute.cur_close = 98.0;
  ctx.compute.ma_ref = 100.0;

  TrailingMa20CloseCondition cond;
  cond.op = "<";
  cond.ma_ref_relax = 0.01;
  cond.poll_sec = 10.0;
  const EvalResult out = EvaluateTrailingMa20Close(cond, ctx);
  EXPECT_TRUE(out.hit);
  EXPECT_DOUBLE_EQ(out.right_value, 99.0);
}

TEST(TrailingMa20RealtimeEval, BuySideBelowMaHits) {
  TrailingMa20RtRuntime rt;
  TrailingMa20RtEvalContext ctx;
  ctx.now = 1.0;
  ctx.runtime = &rt;
  ctx.side = "BUY";
  ctx.fields["bid_1"] = 95.0;
  ctx.has_compute = true;
  ctx.compute.ok = true;
  ctx.compute.ma_ref = 100.0;

  TrailingMa20RealtimeCondition cond;
  cond.price_field = "bid_1";
  cond.op = "<";
  const EvalResult out = EvaluateTrailingMa20Realtime(cond, ctx);
  EXPECT_TRUE(out.hit);
}

TEST(PreEntryLowStopEval, LongPositionStopHit) {
  PreEntryLowStopRuntime rt;
  rt.stop_levels["rule:0"] = 98.0;
  PreEntryLowStopEvalContext ctx;
  ctx.runtime = &rt;
  ctx.rule_id = "rule";
  ctx.cond_idx = 0;
  ctx.has_pos_side = true;
  ctx.pos_side = "BUY";
  ctx.fields["bid_1"] = 97.5;

  PreEntryBarsLowStopCondition cond;
  cond.price_field = "bid_1";
  const EvalResult out = EvaluatePreEntryBarsLowStop(cond, ctx);
  EXPECT_TRUE(out.hit);
  EXPECT_EQ(out.op, "<=");
}

TEST(SessionCloseUtil, InWindowBeforeRthClose) {
  LocalDateTime now;
  now.year = 2026;
  now.month = 8;
  now.day = 10;
  now.hour = 15;
  now.minute = 55;
  now.weekday = 0;
  const mmtrade::engine::SessionCloseWindowResult win = SessionCloseWindowRthLocal(now, 10);
  EXPECT_TRUE(win.ok);
  EXPECT_EQ(win.note, "in_window");
}

TEST(BeforeSessionCloseEval, UsesLocalClockContext) {
  BeforeSessionCloseCondition cond;
  cond.minutes_before = 10;
  cond.session = "rth";
  BeforeSessionCloseEvalContext ctx;
  ctx.has_local_now = true;
  ctx.local_now.year = 2026;
  ctx.local_now.month = 8;
  ctx.local_now.day = 10;
  ctx.local_now.hour = 15;
  ctx.local_now.minute = 49;
  ctx.local_now.weekday = 0;
  const EvalResult miss = EvaluateBeforeSessionClose(cond, ctx);
  EXPECT_FALSE(miss.hit);
  ctx.local_now.minute = 55;
  const EvalResult hit = EvaluateBeforeSessionClose(cond, ctx);
  EXPECT_TRUE(hit.hit);
}

TEST(StagedTakeProfitUtil, ResolveBaselineQty) {
  EXPECT_DOUBLE_EQ(ResolveStagedBaselineQty(563.0, 199.0), 563.0);
  EXPECT_DOUBLE_EQ(ResolveStagedBaselineQty(200.0, 563.0), 563.0);
}

TEST(StagedTakeProfitUtil, AllocateTrancheQtysMatchesPython) {
  {
    const std::vector<double> q = AllocateTrancheQtys(563.0, {0.5, 0.5}, 1);
    ASSERT_EQ(q.size(), 2u);
    EXPECT_DOUBLE_EQ(q[0], 281.0);
    EXPECT_DOUBLE_EQ(q[1], 282.0);
    EXPECT_NEAR(q[0] + q[1], 563.0, 1e-9);
  }
  {
    const std::vector<double> q = AllocateTrancheQtys(600.0, {0.5, 0.5}, 200);
    ASSERT_EQ(q.size(), 2u);
    EXPECT_DOUBLE_EQ(q[0], 200.0);
    EXPECT_DOUBLE_EQ(q[1], 400.0);
    EXPECT_NEAR(q[0] + q[1], 600.0, 1e-9);
  }
  {
    const std::vector<double> q = AllocateTrancheQtys(563.0, {0.5, 0.3, 0.2}, 1);
    ASSERT_EQ(q.size(), 3u);
    EXPECT_DOUBLE_EQ(q[0], 281.0);
    EXPECT_NEAR(q[0] + q[1] + q[2], 563.0, 1e-9);
  }
  {
    const std::vector<double> q = AllocateTrancheQtys(500.0, {0.5, 0.5}, 200);
    ASSERT_EQ(q.size(), 2u);
    EXPECT_NEAR(q[0] + q[1], 500.0, 1e-9);
    EXPECT_DOUBLE_EQ(q[1], 300.0);
  }
}

TEST(StagedTakeProfitEval, HitsWhenGainCrossesFirstTarget) {
  StagedTakeProfitCondition cond;
  cond.targets = {0.05, 0.10};
  cond.fractions = {0.5, 0.5};
  cond.price_field = "bid_1";

  StagedTpRuntime rt;
  StagedTpEvalContext ctx;
  ctx.runtime = &rt;
  ctx.rule_id = "exit1";
  ctx.cond_idx = 0;
  ctx.sym.opened_qty = 563.0;
  ctx.sym.initial_qty = 563.0;
  ctx.sym.opened_price = 100.0;
  ctx.sym.closed_qty = 0.0;
  ctx.fields["bid_1"] = 106.0;

  const EvalResult out = EvaluateStagedTakeProfit(cond, ctx);
  EXPECT_TRUE(out.handled);
  EXPECT_TRUE(out.hit);
  EXPECT_EQ(out.reason, "staged_take_profit");
}

TEST(TrailingTakeProfitEval, BuySideTriggersBelowThreshold) {
  TrailingTpRuntime rt;
  TrailingTpEvalContext ctx;
  ctx.now = 100.0;
  ctx.runtime = &rt;
  ctx.side = "BUY";
  ctx.fields["bid_1"] = 95.0;
  ctx.has_compute = true;
  ctx.compute.ok = true;
  ctx.compute.current_px = 95.0;
  ctx.compute.extreme = 100.0;
  ctx.compute.trigger = 96.0;
  ctx.compute.ref_bar = "K_15M";

  TrailingTakeProfitCondition cond;
  cond.n_pct = 0.08;
  cond.price_field = "bid_1";
  const EvalResult out = EvaluateTrailingTakeProfit(cond, ctx);
  EXPECT_TRUE(out.handled);
  EXPECT_TRUE(out.hit);
  EXPECT_EQ(out.reason, "trailing_take_profit");
  EXPECT_EQ(out.op, "<");
}

TEST(TrailingTakeProfitEval, MissingDependency) {
  TrailingTakeProfitCondition cond;
  const EvalResult out = EvaluateTrailingTakeProfit(cond, TrailingTpEvalContext());
  EXPECT_FALSE(out.handled);
  EXPECT_EQ(out.reason, "missing_dependency");
}

TEST(ConditionBridge, CopiesTypedFieldsIntoBundles) {
  mmtrade::config::Condition cond;
  cond.kind = ConditionKind::kStagedTakeProfit;
  cond.staged_tp.price_field = "bid_1";
  cond.staged_tp.targets = {0.05};
  cond.staged_tp.fractions = {1.0};

  ExitEvalBundle exit_bundle;
  FillExitEvalBundle(cond, &exit_bundle);
  EXPECT_EQ(exit_bundle.kind, ConditionKind::kStagedTakeProfit);
  ASSERT_EQ(exit_bundle.staged_tp.targets.size(), 1u);
  EXPECT_DOUBLE_EQ(exit_bundle.staged_tp.targets[0], 0.05);

  cond.kind = ConditionKind::kTrailingTakeProfit;
  cond.trailing_tp.n_pct = 0.08;
  EntryEvalBundle entry;
  FillEntryEvalBundle(cond, &entry);
  EXPECT_EQ(entry.kind, ConditionKind::kTrailingTakeProfit);
  EXPECT_DOUBLE_EQ(entry.trailing_tp.n_pct, 0.08);
}

TEST(Registry, DispatchesEntryVolumeAndExitPnl) {
  VolumeRuntime rt;
  VolumeEvalContext vctx;
  vctx.now = 1.0;
  vctx.runtime = &rt;
  vctx.has_compute = true;
  vctx.compute.hit = true;
  vctx.compute.vol = 100.0;
  vctx.compute.thresh = 50.0;

  EntryEvalBundle entry;
  entry.kind = ConditionKind::kVolume;
  entry.volume.poll_sec = 30.0;
  entry.volume_ctx = vctx;
  EXPECT_TRUE(EvaluateEntryCondition(entry).hit);

  ExitEvalBundle exit_bundle;
  exit_bundle.kind = ConditionKind::kPnl;
  exit_bundle.pnl.op = ">=";
  exit_bundle.pnl.value = 0.05;
  exit_bundle.pnl_ctx.has_gain_fraction = true;
  exit_bundle.pnl_ctx.gain_fraction = 0.1;
  EXPECT_TRUE(EvaluateExitCondition(exit_bundle).hit);

  TrailingTpRuntime trailing_rt;
  EntryEvalBundle trailing;
  trailing.kind = ConditionKind::kTrailingTakeProfit;
  trailing.trailing_tp_ctx.runtime = &trailing_rt;
  trailing.trailing_tp_ctx.has_compute = true;
  trailing.trailing_tp_ctx.compute.ok = true;
  trailing.trailing_tp_ctx.compute.current_px = 95.0;
  trailing.trailing_tp_ctx.compute.trigger = 96.0;
  trailing.trailing_tp_ctx.side = "BUY";
  trailing.trailing_tp_ctx.fields["bid_1"] = 95.0;
  trailing.trailing_tp_ctx.now = 1.0;
  EXPECT_TRUE(EvaluateEntryCondition(trailing).hit);

  StagedTpRuntime staged_rt;
  StagedTpEvalContext staged_ctx;
  staged_ctx.runtime = &staged_rt;
  staged_ctx.sym.opened_qty = 100.0;
  staged_ctx.sym.initial_qty = 100.0;
  staged_ctx.sym.opened_price = 10.0;
  staged_ctx.fields["bid_1"] = 11.0;

  ExitEvalBundle staged;
  staged.kind = ConditionKind::kStagedTakeProfit;
  staged.staged_tp.targets = {0.05};
  staged.staged_tp.fractions = {1.0};
  staged.staged_tp_ctx = staged_ctx;
  EXPECT_TRUE(EvaluateExitCondition(staged).hit);

  EntryEvalBundle unknown;
  unknown.kind = ConditionKind::kBeforeSessionClose;
  const EvalResult missing = EvaluateEntryCondition(unknown);
  EXPECT_FALSE(missing.handled);
  EXPECT_EQ(missing.reason, "not_registered");
}
