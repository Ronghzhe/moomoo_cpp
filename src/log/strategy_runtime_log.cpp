#include "log/strategy_runtime_log.hpp"

#include "log/ledger_ts.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace mmtrade {
namespace log {

namespace {

void LogIf(StrategyStore *store, const std::string &level, const std::string &msg) {
  if (store != NULL) {
    store->LogRuntimeLine(level, msg);
  }
}

std::string FmtQty(double q) {
  const double r = static_cast<double>(static_cast<long long>(q + (q >= 0 ? 0.5 : -0.5)));
  if (std::abs(q - r) < 1e-9) {
    std::ostringstream oss;
    oss << static_cast<long long>(r);
    return oss.str();
  }
  std::ostringstream oss;
  oss << q;
  return oss.str();
}

}  // namespace

void LogTrdContext(StrategyStore *store, const config::StrategyConfig &cfg,
                   const std::string &trd_env) {
  std::ostringstream oss;
  oss << "交易上下文/trd_context | trd_env=" << trd_env << " trd_market=" << cfg.trd_market
      << " (cfg) | acc_id=" << cfg.acc_id << " acc_index=" << cfg.acc_index;
  LogIf(store, "INFO", oss.str());
}

void LogMonitorStartupBlock(StrategyStore *store, const config::StrategyConfig &cfg,
                            const config::SymbolConfig &sym, const std::string &trd_env) {
  if (store == NULL) {
    return;
  }
  store->LogRuntimeLine("INFO", "");
  store->LogRuntimeBlock("INFO", FormatMonitorStartupMessage(cfg, sym, trd_env));
  store->LogRuntimeLine("INFO", "启动: 策略进程已上线");
  store->LogRuntimeLine("INFO", "");
}

void LogStrategyReady(StrategyStore *store, const std::string &symbol, bool telegram_enabled,
                      bool startup_notify_once) {
  if (store == NULL) {
    return;
  }
  store->LogRuntimeLine("INFO", "");
  store->LogRuntimeLine("INFO", "========== 运行中 / RUNNING ==============================");
  store->LogRuntimeLine("INFO", "  [RUN] 本策略已上线 / strategy is live");
  store->LogRuntimeLine("INFO", "  订阅 / subscribe: ORDER_BOOK  |  " + symbol +
                                   " 推送已开 / push on");
  if (telegram_enabled) {
    store->LogRuntimeLine(
        "INFO",
        "  Telegram/notify: 启用 / enabled（startup_notify_once=" +
            std::string(startup_notify_once ? "true" : "false") +
            "；开/平仓报单结果亦推送）");
  } else {
    store->LogRuntimeLine("INFO", "  Telegram/notify: 未启用 / disabled");
  }
  store->LogRuntimeLine("INFO", "  —— 以下为运行期日志 / runtime log below —— ");
  store->LogRuntimeLine("INFO", "==========================================================");
  store->LogRuntimeLine("INFO", "");
}

void LogLotSize(StrategyStore *store, const std::string &code, int lot_size) {
  std::ostringstream oss;
  oss << "lot_size/手 | code=" << code << " value=" << lot_size << " (snapshot)";
  LogIf(store, "INFO", oss.str());
}

void LogReadMarket(StrategyStore *store, const std::string &code, double bid, double ask) {
  std::ostringstream oss;
  oss << "读取行情/read_market | code=" << code << " bid_1=" << bid << " ask_1=" << ask;
  LogIf(store, "INFO", oss.str());
}

void LogEntryJudge(StrategyStore *store, const std::string &code, const std::string &rule_id,
                   int cond_idx, const std::string &type_name, const std::string &reason, bool ok,
                   double plan_qty, double est_notional) {
  std::ostringstream oss;
  oss << "entry_judge/开仓子条件 | code=" << code << " rule=" << rule_id << " #" << cond_idx
      << " type=" << type_name << " | " << reason << " ok=" << (ok ? "True" : "False")
      << " | plan_qty=" << FmtQty(plan_qty) << " est_notional~" << std::fixed
      << std::setprecision(2) << est_notional;
  LogIf(store, "INFO", oss.str());
}

void LogStartTrade(StrategyStore *store, const std::string &code, const std::string &hit_label) {
  std::ostringstream oss;
  oss << "开始交易/start_trade | code=" << code << " entry_hit=" << hit_label;
  LogIf(store, "INFO", oss.str());
}

void LogExitJudge(StrategyStore *store, const std::string &code, const std::string &rule_id,
                  int cond_idx, const std::string &type_name, const std::string &reason, bool ok,
                  double plan_qty, double est_notional) {
  std::ostringstream oss;
  oss << "exit_judge/平仓子条件 | code=" << code << " rule=" << rule_id << " #" << cond_idx
      << " type=" << type_name << " | " << reason << " ok=" << (ok ? "True" : "False")
      << " | plan_exit_qty=" << FmtQty(plan_qty) << " est_notional~" << std::fixed
      << std::setprecision(2) << est_notional;
  LogIf(store, "INFO", oss.str());
}

void LogStartExit(StrategyStore *store, const std::string &code, const std::string &hit_label) {
  std::ostringstream oss;
  oss << "平仓触发/exit_hit | code=" << code << " " << hit_label;
  LogIf(store, "INFO", oss.str());
}

void LogOrderSizing(StrategyStore *store, const config::SymbolConfig &sym, double ref_px,
                    double qty, int lot_size, const std::string &note) {
  std::ostringstream oss;
  oss << "报单计算/order_sizing | 参考价/px=" << std::fixed << std::setprecision(2) << ref_px;
  if (sym.budget > 0.0) {
    oss << " | budget_total=" << std::setprecision(2) << sym.budget;
  } else if (sym.entry_qty > 0.0) {
    oss << " | entry_qty=" << FmtQty(sym.entry_qty);
  }
  oss << " | lot_size=" << lot_size << " => qty=" << FmtQty(qty);
  if (!note.empty()) {
    oss << " | " << note;
  }
  const double est = ref_px * qty;
  oss << " | est_notional~" << std::setprecision(2) << est;
  LogIf(store, "INFO", oss.str());
}

void LogOrderRequest(StrategyStore *store, const config::SymbolConfig &sym, const std::string &side,
                     double qty, double submit_px, double ref_px, const std::string &trd_env,
                     const std::string &remark) {
  std::ostringstream oss;
  oss << "报单提交/order_request | order_type=" << sym.order_type;
  if (sym.order_type.find("sim") != std::string::npos ||
      sym.order_type.find("SIM") != std::string::npos) {
    oss << " sim_market_pct=" << std::fixed << std::setprecision(4) << sym.sim_market_pct;
  }
  oss << " ref_px=" << std::setprecision(2) << ref_px << " submit_price=" << submit_px
      << " time_in_force=" << sym.time_in_force << " side=" << side << " code=" << sym.code
      << " qty=" << FmtQty(qty) << " est_notional~" << std::setprecision(2) << (submit_px * qty)
      << " remark=" << remark << " trd_env=" << trd_env;
  LogIf(store, "INFO", oss.str());
}

void LogPlaceOrderResult(StrategyStore *store, bool ok, const std::string &order_id,
                         const std::string &status, const std::string &error) {
  std::ostringstream oss;
  if (ok) {
    oss << "下单结果/place_order_result | ret=0 order_id=" << order_id << " status=" << status;
  } else {
    oss << "下单结果/place_order_result | ret=-1 error=" << error;
  }
  LogIf(store, ok ? "INFO" : "WARN", oss.str());
}

void LogAwaitFill(StrategyStore *store, const std::string &order_id, bool is_entry) {
  std::ostringstream oss;
  if (is_entry) {
    oss << "已报单，等待成交/await_order_fill | order_id=" << order_id
        << " (未成仓前不写 JSON、不发成交 Telegram)";
  } else {
    oss << "平仓已报单，等待成交/exit_await_fill | order_id=" << order_id
        << " (未平仓结前不写 status=3、不发平仓成交 Telegram)";
  }
  LogIf(store, "INFO", oss.str());
}

void LogOrderPoll(StrategyStore *store, const std::string &code, const std::string &order_id,
                  const std::string &status, double dealt, double target, bool is_exit,
                  const std::string &outcome) {
  std::ostringstream oss;
  oss << (is_exit ? "订单轮询/order_poll_exit" : "订单轮询/order_poll") << " | code=" << code
      << " order_id=" << order_id << " status=" << status << " dealt=" << FmtQty(dealt)
      << " target=" << FmtQty(target) << " -> " << outcome;
  LogIf(store, "INFO", oss.str());
}

void LogEntryFillBlock(StrategyStore *store, const config::StrategyConfig &cfg,
                       const config::SymbolConfig &sym, double fill_qty, double fill_px,
                       double exp_px, const std::string &order_id, const std::string &trd_env,
                       const std::string &hit_rule_id) {
  if (store == NULL) {
    return;
  }
  store->LogRuntimeLine("INFO", "");
  store->LogRuntimeBlock("INFO", FormatEntryFillMessage(cfg, sym, fill_qty, fill_px, exp_px,
                                                       order_id, trd_env, hit_rule_id));
  store->LogRuntimeLine("INFO", "");
}

void LogExitFillBlock(StrategyStore *store, const config::StrategyConfig &cfg,
                      const config::SymbolConfig &sym, double fill_qty, double fill_px,
                      double exp_px, const std::string &order_id, const std::string &trd_env,
                      const std::string &exit_rule_id) {
  if (store == NULL) {
    return;
  }
  store->LogRuntimeLine("INFO", "");
  store->LogRuntimeBlock(
      "INFO", FormatExitFillMessage(cfg, sym, sym.exit.side, fill_qty, fill_px, exp_px,
                                    sym.opened_price, order_id, exit_rule_id, trd_env));
  store->LogRuntimeLine("INFO", "");
}

}  // namespace log
}  // namespace mmtrade
