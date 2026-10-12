#include "strategy/eval_bundle_factory.hpp"

#include "config/condition_bridge.hpp"
#include "strategy/live_eval_inject.hpp"
#include "strategy/trade_side_util.hpp"

namespace mmtrade {
namespace strategy {

void BuildEntryEvalBundle(const config::Condition &cond, const StrategyTickContext &tick,
                          SymbolEvalRuntime *rt, const std::string &entry_side,
                          const std::string &rule_id, int cond_idx, LiveEvalCompute *live,
                          engine::EntryEvalBundle *out) {
  if (out == NULL || rt == NULL) {
    return;
  }
  config::FillEntryEvalBundle(cond, out);
  out->fields = tick.fields;
  if (tick.has_bid_ask) {
    out->fields["bid_1"] = tick.bid_1;
    out->fields["ask_1"] = tick.ask_1;
  }

  out->volume_ctx.now = tick.now;
  out->volume_ctx.runtime = &rt->volume;

  out->kline_close_ctx.now = tick.now;
  out->kline_close_ctx.runtime = &rt->kline_close;

  out->kline_candle_ctx.now = tick.now;
  out->kline_candle_ctx.runtime = &rt->kline_candle;
  out->kline_candle_ctx.rule_id = rule_id;
  out->kline_candle_ctx.cond_idx = cond_idx;

  out->trailing_tp_ctx.now = tick.now;
  out->trailing_tp_ctx.runtime = &rt->trailing_tp;
  out->trailing_tp_ctx.side = entry_side;
  out->trailing_tp_ctx.bid_1 = tick.bid_1;
  out->trailing_tp_ctx.ask_1 = tick.ask_1;
  out->trailing_tp_ctx.has_bid_ask = tick.has_bid_ask;
  out->trailing_tp_ctx.fields = out->fields;

  out->day_open_gain_ctx.runtime = &rt->day_open_gain;
  out->day_open_gain_ctx.bid_1 = tick.bid_1;
  out->day_open_gain_ctx.ask_1 = tick.ask_1;
  out->day_open_gain_ctx.has_bid_ask = tick.has_bid_ask;

  out->trailing_ma20_close_ctx.now = tick.now;
  out->trailing_ma20_close_ctx.runtime = &rt->trailing_ma20_close;
  out->trailing_ma20_close_ctx.side = entry_side;

  out->trailing_ma20_rt_ctx.now = tick.now;
  out->trailing_ma20_rt_ctx.runtime = &rt->trailing_ma20_rt;
  out->trailing_ma20_rt_ctx.side = entry_side;
  out->trailing_ma20_rt_ctx.use_pos_side = false;
  out->trailing_ma20_rt_ctx.bid_1 = tick.bid_1;
  out->trailing_ma20_rt_ctx.ask_1 = tick.ask_1;
  out->trailing_ma20_rt_ctx.has_bid_ask = tick.has_bid_ask;
  out->trailing_ma20_rt_ctx.fields = out->fields;

  if (live != NULL) {
    InjectLiveEntryEval(cond, live, tick, out);
  }
}

void BuildExitEvalBundle(const config::Condition &cond, const config::SymbolConfig &sym,
                         const StrategyTickContext &tick, SymbolEvalRuntime *rt,
                         const std::string &exit_side, const std::string &rule_id, int cond_idx,
                         LiveEvalCompute *live, engine::ExitEvalBundle *out) {
  if (out == NULL || rt == NULL) {
    return;
  }
  config::FillExitEvalBundle(cond, out);
  out->fields = tick.fields;
  if (tick.has_bid_ask) {
    out->fields["bid_1"] = tick.bid_1;
    out->fields["ask_1"] = tick.ask_1;
  }

  out->pnl_ctx.has_gain_fraction = tick.has_gain_fraction;
  out->pnl_ctx.gain_fraction = tick.gain_fraction;

  out->kline_close_ctx.now = tick.now;
  out->kline_close_ctx.runtime = &rt->kline_close;

  out->trailing_tp_ctx.now = tick.now;
  out->trailing_tp_ctx.runtime = &rt->trailing_tp;
  out->trailing_tp_ctx.side = exit_side;
  out->trailing_tp_ctx.bid_1 = tick.bid_1;
  out->trailing_tp_ctx.ask_1 = tick.ask_1;
  out->trailing_tp_ctx.has_bid_ask = tick.has_bid_ask;
  out->trailing_tp_ctx.fields = out->fields;

  out->staged_tp_ctx.runtime = &rt->staged_tp;
  out->staged_tp_ctx.rule_id = rule_id;
  out->staged_tp_ctx.cond_idx = cond_idx;
  out->staged_tp_ctx.fields = out->fields;
  out->staged_tp_ctx.sym.opened_qty = sym.opened_qty;
  out->staged_tp_ctx.sym.initial_qty = sym.initial_qty;
  out->staged_tp_ctx.sym.opened_price = sym.opened_price;
  out->staged_tp_ctx.sym.closed_qty = sym.closed_qty;

  out->trailing_ma20_close_ctx.now = tick.now;
  out->trailing_ma20_close_ctx.runtime = &rt->trailing_ma20_close;
  out->trailing_ma20_close_ctx.side = exit_side;

  out->trailing_ma20_rt_ctx.now = tick.now;
  out->trailing_ma20_rt_ctx.runtime = &rt->trailing_ma20_rt;
  out->trailing_ma20_rt_ctx.use_pos_side = true;
  out->trailing_ma20_rt_ctx.pos_side = PositionDirectionFromExitSide(exit_side);
  out->trailing_ma20_rt_ctx.bid_1 = tick.bid_1;
  out->trailing_ma20_rt_ctx.ask_1 = tick.ask_1;
  out->trailing_ma20_rt_ctx.has_bid_ask = tick.has_bid_ask;
  out->trailing_ma20_rt_ctx.fields = out->fields;

  out->before_session_close_ctx.code = sym.code;
  out->before_session_close_ctx.has_local_now = tick.has_exchange_local_now;
  out->before_session_close_ctx.local_now = tick.exchange_local_now;

  out->pre_entry_low_stop_ctx.runtime = &rt->pre_entry_low_stop;
  out->pre_entry_low_stop_ctx.rule_id = rule_id;
  out->pre_entry_low_stop_ctx.cond_idx = cond_idx;
  out->pre_entry_low_stop_ctx.fields = out->fields;
  out->pre_entry_low_stop_ctx.bid_1 = tick.bid_1;
  out->pre_entry_low_stop_ctx.ask_1 = tick.ask_1;
  out->pre_entry_low_stop_ctx.has_bid_ask = tick.has_bid_ask;
  out->pre_entry_low_stop_ctx.has_pos_side = true;
  out->pre_entry_low_stop_ctx.pos_side = PositionDirectionFromExitSide(exit_side);

  if (live != NULL) {
    InjectLiveExitEval(cond, live, tick, out);
  }
}

}  // namespace strategy
}  // namespace mmtrade
