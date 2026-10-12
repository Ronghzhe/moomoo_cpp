#pragma once

#include <map>
#include <string>
#include <vector>

namespace mmtrade {
namespace engine {

struct PriceCondition {
  std::string field;
  std::string op;
  bool has_value;
  double value;

  PriceCondition() : has_value(false), value(0.0) {}
};

struct PnlCondition {
  std::string op;
  double value;

  PnlCondition() : value(0.0) {}
};

struct KlineCloseCondition {
  std::string kl_type;
  std::string op;
  bool has_value;
  double value;
  double poll_sec;
  int after_open_minutes;

  KlineCloseCondition()
      : has_value(false), value(0.0), poll_sec(30.0), after_open_minutes(0) {}
};

struct VolumeEntryWhen {
  std::string kl_type;
  int ma_period;
  double volume_multiple;
  bool ma_include_last;
  double poll_sec;

  VolumeEntryWhen()
      : ma_period(20), volume_multiple(2.0), ma_include_last(true), poll_sec(30.0) {}
};

struct KlineCandleCondition {
  std::string kl_type;
  int candle;
  double poll_sec;
  int after_open_minutes;
  std::string candle_ref;
  std::string eval_mode;

  KlineCandleCondition()
      : candle(1),
        poll_sec(30.0),
        after_open_minutes(0),
        candle_ref("prev_close"),
        eval_mode("repeat") {}
};

struct KlineCandleShapeResult {
  bool hit;
  std::string shape_rule;
  double ref_px;
  std::string candle_ref_norm;
  std::string cmp_note;

  KlineCandleShapeResult() : hit(false), ref_px(0.0) {}
};

struct TrailingTakeProfitCondition {
  std::string kl_type;
  double n_pct;
  std::string price_field;
  std::string op;

  TrailingTakeProfitCondition()
      : n_pct(0.08), price_field("bid_1") {}
};

struct StagedTakeProfitCondition {
  std::string price_field;
  std::vector<double> targets;
  std::vector<double> fractions;

  StagedTakeProfitCondition() : price_field("bid_1") {}
};

struct DayOpenGainCondition {
  double gain_pct;
  std::string ref;

  DayOpenGainCondition() : gain_pct(0.0), ref("mid") {}
};

struct TrailingMa20CloseCondition {
  std::string kl_type;
  int ma_period;
  std::string op;
  double ma_ref_relax;
  double poll_sec;

  TrailingMa20CloseCondition()
      : ma_period(20), ma_ref_relax(0.0), poll_sec(10.0), kl_type("K_15M") {}
};

struct TrailingMa20RealtimeCondition {
  std::string kl_type;
  int ma_period;
  std::string price_field;
  std::string op;
  double poll_sec;

  TrailingMa20RealtimeCondition()
      : ma_period(20), price_field("bid_1"), poll_sec(3.0), kl_type("K_15M") {}
};

struct BeforeSessionCloseCondition {
  int minutes_before;
  std::string session;
  double poll_sec;

  BeforeSessionCloseCondition() : minutes_before(10), session("rth"), poll_sec(30.0) {}
};

struct PreEntryBarsLowStopCondition {
  std::string kl_type;
  int bars;
  double low_offset;
  std::string price_field;
  double poll_sec;
  double log_interval_sec;
  bool clip_to_rth_open;
  bool same_calendar_day_only;

  PreEntryBarsLowStopCondition()
      : bars(2),
        low_offset(0.005),
        price_field("bid_1"),
        poll_sec(30.0),
        log_interval_sec(20.0),
        clip_to_rth_open(true),
        same_calendar_day_only(true),
        kl_type("K_15M") {}
};

typedef std::map<std::string, double> FieldMap;

struct EvalResult {
  bool handled;
  bool hit;
  std::string reason;
  bool has_left_value;
  double left_value;
  bool has_right_value;
  double right_value;
  std::string op;
  std::string field;

  EvalResult()
      : handled(false),
        hit(false),
        has_left_value(false),
        left_value(0.0),
        has_right_value(false),
        right_value(0.0) {}
};

}  // namespace engine
}  // namespace mmtrade
