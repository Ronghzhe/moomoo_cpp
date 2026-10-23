#pragma once

#include "engine/condition_types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace mmtrade {
namespace config {

enum class ConditionKind {
  kPrice,
  kDayOpenGain,
  kVolume,
  kKlineClose,
  kKlineCandle,
  kTrailingTakeProfit,
  kTrailingMa20Close,
  kTrailingMa20Realtime,
  kPnl,
  kStagedTakeProfit,
  kBeforeSessionClose,
  kPreEntryBarsLowStop,
};

const char *ConditionPythonName(ConditionKind kind);

struct Condition {
  ConditionKind kind;
  engine::PriceCondition price;
  engine::PnlCondition pnl;
  engine::VolumeEntryWhen volume;
  engine::KlineCloseCondition kline_close;
  engine::KlineCandleCondition kline_candle;
  engine::TrailingTakeProfitCondition trailing_tp;
  engine::StagedTakeProfitCondition staged_tp;
  engine::DayOpenGainCondition day_open_gain;
  engine::TrailingMa20CloseCondition trailing_ma20_close;
  engine::TrailingMa20RealtimeCondition trailing_ma20_rt;
  engine::BeforeSessionCloseCondition before_session_close;
  engine::PreEntryBarsLowStopCondition pre_entry_low_stop;
};

struct EntryStrategyRule {
  std::string rule_id;
  bool enabled;
  std::string logic;
  std::vector<Condition> conditions;
};

struct ExitStrategyRule {
  std::string rule_id;
  bool enabled;
  std::string logic;
  std::string side;
  std::vector<Condition> conditions;
};

struct ExitConfig {
  bool enabled;
  std::string side;
};

struct SymbolConfig {
  std::string task_id;
  bool enabled;
  std::string code;
  double budget;
  std::string entry_side;
  ExitConfig exit;
  std::vector<EntryStrategyRule> entry_strategies;
  std::string entry_strategy_logic;
  std::vector<ExitStrategyRule> exit_strategies;
  std::string exit_strategy_logic;
  int status = 0;
  double opened_qty = 0.0;
  double opened_price = 0.0;
  double closed_qty = 0.0;
  double closed_price = 0.0;
  double entry_qty = -1.0;  // <=0 means null
  double initial_qty = 0.0;
  double exit_qty = 0.0;  // 0 = flat open remainder; >0 = per-order exit cap
  std::string order_type;
  double sim_market_pct;
  std::string time_in_force;
  std::string order_session;
  bool startup_notify_once;
};

struct StrategyConfig {
  int version;
  std::string client_name;
  std::string strategy_name;
  std::string mode;
  std::string opend_host;
  int opend_port;
  std::string trd_market;
  std::int64_t acc_id;
  int acc_index;
  std::string order_type;
  std::string time_in_force;
  double order_poll_min_sec;
  int order_retry_max;
  double order_cancel_retry_wait_sec = 2.0;
  double order_limit_price_retry_wait_sec = 0.5;
  double order_too_fast_retry_wait_sec = 1.0;
  double order_rate_limit_retry_wait_sec = 2.0;
  double order_unfilled_replace_sec = 12.0;
  bool auto_stop_after_close;
  bool dingding_enabled;
  std::string csv_dir;
  bool telegram_enabled;
  std::string telegram_bot_token;
  std::string telegram_chat_id_paper;
  std::string telegram_chat_id_real;
  std::vector<SymbolConfig> symbols;
};

}  // namespace config
}  // namespace mmtrade
