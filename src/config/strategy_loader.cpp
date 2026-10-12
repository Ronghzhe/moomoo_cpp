#include "config/strategy_loader.hpp"

#include "config/execution_util.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace mmtrade {
namespace config {
namespace {

using json = nlohmann::json;

void RequireObject(const json &j, const char *label) {
  if (!j.is_object()) {
    throw std::runtime_error(std::string(label) + " must be object");
  }
}

std::string Trim(const std::string &s) {
  size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  size_t e = s.size();
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}

std::string Lower(const std::string &s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

double JsonToDouble(const json &j, double default_v = 0.0) {
  if (j.is_null() || !j.is_number()) {
    return default_v;
  }
  return j.get<double>();
}

bool JsonIsEmpty(const json &j) {
  return j.is_null() || (j.is_string() && Trim(j.get<std::string>()).empty());
}

Condition ParsePriceCondition(const json &d, const std::string &label, bool require_value) {
  RequireObject(d, label.c_str());
  Condition c;
  c.kind = ConditionKind::kPrice;
  c.price.field = Trim(d.value("field", std::string()));
  c.price.op = Trim(d.value("op", std::string()));
  if (c.price.op.empty()) {
    return c;
  }
  if (c.price.field.empty()) {
    throw std::runtime_error(label + " missing field");
  }
  if (JsonIsEmpty(d.at("value"))) {
    if (require_value) {
      throw std::runtime_error(label + " missing value");
    }
  } else {
    c.price.has_value = true;
    c.price.value = JsonToDouble(d.at("value"));
  }
  return c;
}

Condition ParseDayOpenGain(const json &d, const std::string &label) {
  RequireObject(d, label.c_str());
  Condition c;
  c.kind = ConditionKind::kDayOpenGain;
  if (JsonIsEmpty(d.at("gain_pct"))) {
    throw std::runtime_error(label + " missing gain_pct");
  }
  c.day_open_gain.gain_pct = JsonToDouble(d.at("gain_pct"));
  c.day_open_gain.ref = Lower(Trim(d.value("ref", std::string("mid"))));
  if (c.day_open_gain.ref != "mid" && c.day_open_gain.ref != "ask_1" &&
      c.day_open_gain.ref != "bid_1") {
    throw std::runtime_error(label + ".ref invalid");
  }
  return c;
}

Condition ParseVolumeEntryWhen(const json &d, const std::string &label) {
  RequireObject(d, label.c_str());
  Condition c;
  c.kind = ConditionKind::kVolume;
  c.volume.kl_type = Trim(d.value("kl_type", std::string()));
  if (c.volume.kl_type.empty()) {
    throw std::runtime_error(label + ".kl_type empty");
  }
  c.volume.ma_period = d.value("ma_period", 20);
  if (c.volume.ma_period < 1) {
    throw std::runtime_error(label + ".ma_period must be >= 1");
  }
  c.volume.volume_multiple = JsonToDouble(d.value("volume_multiple", json(2.0)));
  c.volume.ma_include_last = d.value("ma_include_last", true);
  c.volume.poll_sec = JsonToDouble(d.value("poll_sec", json(30.0)));
  if (c.volume.poll_sec <= 0) {
    throw std::runtime_error(label + ".poll_sec must be > 0");
  }
  return c;
}

Condition ParseKlineClose(const json &d, const std::string &label) {
  RequireObject(d, label.c_str());
  Condition c;
  c.kind = ConditionKind::kKlineClose;
  c.kline_close.kl_type = Trim(d.value("kl_type", std::string()));
  if (c.kline_close.kl_type.empty()) {
    throw std::runtime_error(label + ".kl_type empty");
  }
  c.kline_close.op = Trim(d.value("op", std::string()));
  if (!c.kline_close.op.empty() && JsonIsEmpty(d.at("value"))) {
    throw std::runtime_error(label + ".value required when op set");
  }
  if (!JsonIsEmpty(d.at("value"))) {
    c.kline_close.has_value = true;
    c.kline_close.value = JsonToDouble(d.at("value"));
  }
  c.kline_close.poll_sec = JsonToDouble(d.value("poll_sec", json(30.0)));
  c.kline_close.after_open_minutes = d.value("after_open_minutes", 0);
  return c;
}

Condition ParseKlineCandle(const json &d, const std::string &label) {
  RequireObject(d, label.c_str());
  Condition c;
  c.kind = ConditionKind::kKlineCandle;
  c.kline_candle.kl_type = Trim(d.value("kl_type", std::string("K_15M")));
  c.kline_candle.candle = d.value("candle", 1);
  if (c.kline_candle.candle != -1 && c.kline_candle.candle != 0 && c.kline_candle.candle != 1) {
    throw std::runtime_error(label + ".candle must be -1/0/1");
  }
  c.kline_candle.eval_mode = Lower(Trim(d.value("eval_mode", std::string("repeat"))));
  if (c.kline_candle.eval_mode != "repeat" && c.kline_candle.eval_mode != "once") {
    throw std::runtime_error(label + ".eval_mode invalid");
  }
  c.kline_candle.poll_sec = JsonToDouble(d.value("poll_sec", json(30.0)));
  c.kline_candle.after_open_minutes = d.value("after_open_minutes", 0);
  c.kline_candle.candle_ref = Trim(d.value("candle_ref", std::string("prev_close")));
  return c;
}

Condition ParseTrailingTakeProfit(const json &d, const std::string &label) {
  RequireObject(d, label.c_str());
  Condition c;
  c.kind = ConditionKind::kTrailingTakeProfit;
  c.trailing_tp.n_pct = JsonToDouble(d.at("n_pct"));
  if (!(c.trailing_tp.n_pct > 0.0 && c.trailing_tp.n_pct < 1.0)) {
    throw std::runtime_error(label + ".n_pct out of range");
  }
  c.trailing_tp.kl_type = Trim(d.value("kl_type", std::string("K_15M")));
  c.trailing_tp.price_field = Trim(d.value("price_field", std::string("bid_1")));
  c.trailing_tp.op = Trim(d.value("op", std::string()));
  return c;
}

Condition ParseTrailingMa20Close(const json &d, const std::string &label) {
  RequireObject(d, label.c_str());
  Condition c;
  c.kind = ConditionKind::kTrailingMa20Close;
  c.trailing_ma20_close.kl_type = Trim(d.value("kl_type", std::string("K_15M")));
  c.trailing_ma20_close.ma_period = d.value("ma_period", 20);
  if (c.trailing_ma20_close.ma_period < 2) {
    throw std::runtime_error(label + ".ma_period must be >= 2");
  }
  c.trailing_ma20_close.op = Trim(d.value("op", std::string()));
  c.trailing_ma20_close.ma_ref_relax = JsonToDouble(d.value("ma_ref_relax", json(0.0)));
  c.trailing_ma20_close.poll_sec = JsonToDouble(d.value("poll_sec", json(10.0)));
  return c;
}

Condition ParseTrailingMa20Realtime(const json &d, const std::string &label) {
  RequireObject(d, label.c_str());
  Condition c;
  c.kind = ConditionKind::kTrailingMa20Realtime;
  c.trailing_ma20_rt.kl_type = Trim(d.value("kl_type", std::string("K_15M")));
  c.trailing_ma20_rt.ma_period = d.value("ma_period", 20);
  if (c.trailing_ma20_rt.ma_period < 2) {
    throw std::runtime_error(label + ".ma_period must be >= 2");
  }
  c.trailing_ma20_rt.price_field = Trim(d.value("price_field", std::string("bid_1")));
  c.trailing_ma20_rt.op = Trim(d.value("op", std::string()));
  c.trailing_ma20_rt.poll_sec = JsonToDouble(d.value("poll_sec", json(3.0)));
  return c;
}

Condition ParsePnl(const json &d, const std::string &label) {
  RequireObject(d, label.c_str());
  Condition c;
  c.kind = ConditionKind::kPnl;
  c.pnl.op = Trim(d.value("op", std::string()));
  if (c.pnl.op.empty()) {
    throw std::runtime_error(label + ".op missing");
  }
  if (JsonIsEmpty(d.at("value"))) {
    throw std::runtime_error(label + ".value missing");
  }
  c.pnl.value = JsonToDouble(d.at("value"));
  return c;
}

Condition ParseStagedTakeProfit(const json &d, const std::string &label) {
  RequireObject(d, label.c_str());
  Condition c;
  c.kind = ConditionKind::kStagedTakeProfit;
  c.staged_tp.price_field = Trim(d.value("price_field", std::string("bid_1")));
  const json &targets = d.at("targets");
  const json &fractions = d.at("fractions");
  if (!targets.is_array() || !fractions.is_array() || targets.empty() ||
      targets.size() != fractions.size()) {
    throw std::runtime_error(label + " targets/fractions invalid");
  }
  double prev = -1e9;
  for (size_t i = 0; i < targets.size(); ++i) {
    const double v = targets[i].get<double>();
    if (prev >= 0 && v <= prev) {
      throw std::runtime_error(label + " targets must be strictly increasing");
    }
    prev = v;
    c.staged_tp.targets.push_back(v);
    c.staged_tp.fractions.push_back(fractions[i].get<double>());
  }
  return c;
}

Condition ParsePreEntryBarsLowStop(const json &d, const std::string &label) {
  RequireObject(d, label.c_str());
  Condition c;
  c.kind = ConditionKind::kPreEntryBarsLowStop;
  c.pre_entry_low_stop.kl_type = Trim(d.value("kl_type", std::string("K_15M")));
  c.pre_entry_low_stop.bars = d.value("bars", 2);
  if (c.pre_entry_low_stop.bars < 1) {
    throw std::runtime_error(label + ".bars must be >= 1");
  }
  c.pre_entry_low_stop.low_offset = JsonToDouble(d.value("low_offset", json(0.005)));
  if (c.pre_entry_low_stop.low_offset < 0.0 || c.pre_entry_low_stop.low_offset >= 1.0) {
    throw std::runtime_error(label + ".low_offset invalid");
  }
  c.pre_entry_low_stop.price_field = Trim(d.value("price_field", std::string("bid_1")));
  c.pre_entry_low_stop.poll_sec = JsonToDouble(d.value("poll_sec", json(30.0)));
  c.pre_entry_low_stop.log_interval_sec = JsonToDouble(d.value("log_interval_sec", json(20.0)));
  c.pre_entry_low_stop.clip_to_rth_open = d.value("clip_to_rth_open", true);
  c.pre_entry_low_stop.same_calendar_day_only = d.value("same_calendar_day_only", true);
  return c;
}

Condition ParseBeforeSessionClose(const json &d, const std::string &label) {
  RequireObject(d, label.c_str());
  Condition c;
  c.kind = ConditionKind::kBeforeSessionClose;
  c.before_session_close.minutes_before = d.value("minutes_before", 0);
  if (c.before_session_close.minutes_before < 1) {
    throw std::runtime_error(label + ".minutes_before must be >= 1");
  }
  c.before_session_close.session = Lower(Trim(d.value("session", std::string("rth"))));
  if (c.before_session_close.session != "rth") {
    throw std::runtime_error(label + ".session must be rth");
  }
  c.before_session_close.poll_sec = JsonToDouble(d.value("poll_sec", json(30.0)));
  if (c.before_session_close.poll_sec <= 0) {
    throw std::runtime_error(label + ".poll_sec must be > 0");
  }
  return c;
}

Condition ParseEntryCondition(const json &d, const std::string &label) {
  const std::string t = Lower(Trim(d.value("type", std::string())));
  if (t == "orderbook") return ParsePriceCondition(d, label, true);
  if (t == "open") return ParseDayOpenGain(d, label);
  if (t == "volume") return ParseVolumeEntryWhen(d, label);
  if (t == "kline_close") return ParseKlineClose(d, label);
  if (t == "kline_candle" || t == "kline_prev_candle") return ParseKlineCandle(d, label);
  if (t == "trailing_take_profit") return ParseTrailingTakeProfit(d, label);
  if (t == "trailing_ma20_close" || t == "trailing_ma20") return ParseTrailingMa20Close(d, label);
  if (t == "trailing_ma20_realtime") return ParseTrailingMa20Realtime(d, label);
  throw std::runtime_error(label + ".type unsupported: " + t);
}

Condition ParseExitCondition(const json &d, const std::string &label) {
  const std::string t = Lower(Trim(d.value("type", std::string())));
  if (t == "orderbook") return ParsePriceCondition(d, label, true);
  if (t == "pnl_gain_pct" || t == "pnl") return ParsePnl(d, label);
  if (t == "kline_close") return ParseKlineClose(d, label);
  if (t == "trailing_take_profit") return ParseTrailingTakeProfit(d, label);
  if (t == "trailing_ma20_close" || t == "trailing_ma20") return ParseTrailingMa20Close(d, label);
  if (t == "trailing_ma20_realtime") return ParseTrailingMa20Realtime(d, label);
  if (t == "staged_take_profit") return ParseStagedTakeProfit(d, label);
  if (t == "kline_pre_entry_low_stop") return ParsePreEntryBarsLowStop(d, label);
  if (t == "before_session_close" || t == "before_rth_close") {
    return ParseBeforeSessionClose(d, label);
  }
  throw std::runtime_error(label + ".type unsupported: " + t);
}

std::vector<EntryStrategyRule> ParseEntryStrategies(const json &sym, const std::string &label,
                                                    std::string *top_logic) {
  if (!sym.contains("entry_strategies")) {
    *top_logic = "any";
    return std::vector<EntryStrategyRule>();
  }
  const json &raw = sym.at("entry_strategies");
  if (!raw.is_array()) {
    throw std::runtime_error(label + ".entry_strategies must be array");
  }
  *top_logic = Lower(Trim(sym.value("entry_strategy_logic", std::string("any"))));
  if (*top_logic != "any" && *top_logic != "all") {
    throw std::runtime_error(label + ".entry_strategy_logic invalid");
  }
  std::vector<EntryStrategyRule> out;
  for (size_t i = 0; i < raw.size(); ++i) {
    const json &item = raw.at(i);
    const std::string p = label + ".entry_strategies[" + std::to_string(i) + "]";
    RequireObject(item, p.c_str());
    const json &conds = item.at("conditions");
    if (!conds.is_array() || conds.empty()) {
      throw std::runtime_error(p + ".conditions must be non-empty array");
    }
    EntryStrategyRule rule;
    rule.rule_id = Trim(item.value("id", std::string("S") + std::to_string(i + 1)));
    rule.enabled = item.value("enabled", true);
    rule.logic = Lower(Trim(item.value("logic", std::string("all"))));
    if (rule.logic != "all" && rule.logic != "any") {
      throw std::runtime_error(p + ".logic invalid");
    }
    for (size_t j = 0; j < conds.size(); ++j) {
      rule.conditions.push_back(
          ParseEntryCondition(conds.at(j), p + ".conditions[" + std::to_string(j) + "]"));
    }
    out.push_back(rule);
  }
  return out;
}

std::vector<ExitStrategyRule> ParseExitStrategies(const json &sym, const std::string &label,
                                                  const std::string &fallback_side,
                                                  std::string *top_logic) {
  if (!sym.contains("exit_strategies")) {
    *top_logic = "any";
    return std::vector<ExitStrategyRule>();
  }
  const json &raw = sym.at("exit_strategies");
  if (!raw.is_array()) {
    throw std::runtime_error(label + ".exit_strategies must be array");
  }
  *top_logic = Lower(Trim(sym.value("exit_strategy_logic", std::string("any"))));
  if (*top_logic != "any" && *top_logic != "all") {
    throw std::runtime_error(label + ".exit_strategy_logic invalid");
  }
  std::vector<ExitStrategyRule> out;
  for (size_t i = 0; i < raw.size(); ++i) {
    const json &item = raw.at(i);
    const std::string p = label + ".exit_strategies[" + std::to_string(i) + "]";
    RequireObject(item, p.c_str());
    const json &conds = item.at("conditions");
    if (!conds.is_array() || conds.empty()) {
      throw std::runtime_error(p + ".conditions must be non-empty array");
    }
    ExitStrategyRule rule;
    rule.rule_id = Trim(item.value("id", std::string("E") + std::to_string(i + 1)));
    rule.enabled = item.value("enabled", true);
    rule.logic = Lower(Trim(item.value("logic", std::string("all"))));
    rule.side = Trim(item.value("side", fallback_side));
    if (rule.logic != "all" && rule.logic != "any") {
      throw std::runtime_error(p + ".logic invalid");
    }
    for (size_t j = 0; j < conds.size(); ++j) {
      rule.conditions.push_back(
          ParseExitCondition(conds.at(j), p + ".conditions[" + std::to_string(j) + "]"));
    }
    out.push_back(rule);
  }
  return out;
}

SymbolConfig ParseSymbol(const json &sym, const std::string &label,
                         const std::string &default_order_type,
                         const std::string &default_time_in_force) {
  RequireObject(sym, label.c_str());
  SymbolConfig out;
  out.task_id = Trim(sym.value("task_id", sym.value("strategy_id", std::string())));
  out.enabled = sym.value("enabled", true);
  out.code = Trim(sym.at("code").get<std::string>());
  out.entry_side = Trim(sym.at("entry_side").get<std::string>());
  out.status = sym.value("status", 0);
  out.opened_qty = JsonToDouble(sym.value("opened_qty", json(0.0)));
  out.opened_price = JsonToDouble(sym.value("opened_price", json(0.0)));
  out.closed_qty = JsonToDouble(sym.value("closed_qty", json(0.0)));
  out.closed_price = JsonToDouble(sym.value("closed_price", json(0.0)));
  out.entry_qty = -1.0;
  if (sym.contains("entry_qty") && !JsonIsEmpty(sym.at("entry_qty"))) {
    const double eq = JsonToDouble(sym.at("entry_qty"));
    if (eq > 0) out.entry_qty = eq;
  }
  // exit_qty: null/omit => 0 (平清剩余); >0 => 单笔平仓上限。Must init — previously left
  // uninitialized and could make ResolveExitQty return a tiny garbage qty → Invalid quantity.
  out.exit_qty = 0.0;
  if (sym.contains("exit_qty") && !JsonIsEmpty(sym.at("exit_qty"))) {
    const double xq = JsonToDouble(sym.at("exit_qty"));
    if (xq > 0.0 && xq < 1e15) {
      out.exit_qty = xq;
    }
  }
  out.budget = 0.0;
  if (sym.contains("budget") && !JsonIsEmpty(sym.at("budget"))) {
    out.budget = JsonToDouble(sym.at("budget"));
  } else if (sym.contains("budget_usd") && !JsonIsEmpty(sym.at("budget_usd"))) {
    out.budget = JsonToDouble(sym.at("budget_usd"));
  }
  if (sym.contains("order_type") && !JsonIsEmpty(sym.at("order_type"))) {
    out.order_type = NormalizeExecutionOrderType(sym.at("order_type").get<std::string>());
  } else {
    out.order_type = default_order_type;
  }
  if (sym.contains("time_in_force") && !JsonIsEmpty(sym.at("time_in_force"))) {
    out.time_in_force = Trim(sym.at("time_in_force").get<std::string>());
  } else {
    out.time_in_force = default_time_in_force;
  }
  out.sim_market_pct = JsonToDouble(sym.value("sim_market_pct", json(0.05)), 0.05);
  out.startup_notify_once = sym.value("startup_notify_once", true);
  if (sym.contains("initial_qty") && !JsonIsEmpty(sym.at("initial_qty"))) {
    const double iq = JsonToDouble(sym.at("initial_qty"));
    if (iq > 0.0 && iq < 1e15) {
      out.initial_qty = iq;
    }
  }

  const json exit_raw = sym.value("exit", json::object());
  out.exit.side = Trim(exit_raw.value("side", std::string("SELL")));
  out.exit.enabled = exit_raw.value("enabled", true);
  if (sym.contains("exit_enabled")) {
    out.exit.enabled = sym.at("exit_enabled").get<bool>();
  } else if (!sym.contains("exit") && sym.contains("exit_strategies")) {
    out.exit.enabled = false;
    const json &xs = sym.at("exit_strategies");
    if (xs.is_array()) {
      for (const json &item : xs) {
        if (item.value("enabled", false)) {
          out.exit.enabled = true;
          break;
        }
      }
    }
  }

  std::string entry_logic;
  std::string exit_logic;
  out.entry_strategies = ParseEntryStrategies(sym, label, &entry_logic);
  out.entry_strategy_logic = entry_logic;
  out.exit_strategies = ParseExitStrategies(sym, label, out.exit.side, &exit_logic);
  out.exit_strategy_logic = exit_logic;
  return out;
}

std::string SegLabel(const std::string &x) {
  std::string t = std::regex_replace(x, std::regex(R"([\s/\\:;]+)"), "_");
  t = std::regex_replace(t, std::regex("_+"), "_");
  if (!t.empty() && t.front() == '_') t.erase(t.begin());
  if (!t.empty() && t.back() == '_') t.pop_back();
  return t.empty() ? "na" : t;
}

std::string SegStock(const std::string &x) {
  std::string t = x;
  std::transform(t.begin(), t.end(), t.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  for (size_t i = 0; i < t.size(); ++i) {
    if (t[i] == '.') t[i] = '_';
  }
  t = std::regex_replace(t, std::regex(R"([\s/\\:;]+)"), "_");
  t = std::regex_replace(t, std::regex("_+"), "_");
  return t.empty() ? "na" : t;
}

std::string BeijingYmd() {
  std::time_t now = std::time(NULL);
  now += 8 * 3600;
  std::tm tm;
#if defined(_WIN32)
  gmtime_s(&tm, &now);
#else
  gmtime_r(&now, &tm);
#endif
  char buf[16];
  std::strftime(buf, sizeof(buf), "%Y%m%d", &tm);
  return std::string(buf);
}

void EnsureTaskIds(StrategyConfig *cfg) {
  for (size_t i = 0; i < cfg->symbols.size(); ++i) {
    SymbolConfig &sym = cfg->symbols[i];
    if (!Trim(sym.task_id).empty()) continue;
    sym.task_id = SegLabel(cfg->client_name) + "_" + SegStock(sym.code) + "_" +
                  SegLabel(cfg->strategy_name) + "_" + BeijingYmd();
  }
}

std::string MarketPrefix(const std::string &code) {
  const std::string c = Trim(code);
  if (c.size() >= 3 && (c.compare(0, 3, "US.") == 0 || c.compare(0, 3, "HK.") == 0)) {
    return c.substr(0, 2);
  }
  return "";
}

}  // namespace

StrategyConfig LoadConfig(const std::string &path) {
  std::ifstream ifs(path.c_str());
  if (!ifs.good()) {
    throw std::runtime_error("cannot open config: " + path);
  }
  json raw;
  ifs >> raw;

  const json notify_raw = raw.value("notify", json::object());
  const json dd_raw = notify_raw.value("dingding", json::object());
  const bool dingding_enabled = dd_raw.value("enabled", false);

  const json exec_raw = raw.value("execution", json::object());
  const std::string default_order_type =
      NormalizeExecutionOrderType(exec_raw.value("order_type", "NORMAL"));
  const std::string default_time_in_force = Trim(exec_raw.value("time_in_force", "DAY"));

  StrategyConfig cfg;
  cfg.version = raw.value("version", 1);
  cfg.client_name = Trim(raw.value("client_name", std::string()));
  cfg.strategy_name = Trim(raw.value("strategy_name", std::string()));
  cfg.mode = Trim(raw.value("mode", "paper"));
  cfg.opend_host = Trim(raw.at("opend").at("host").get<std::string>());
  cfg.opend_port = raw.at("opend").at("port").get<int>();
  cfg.trd_market = Trim(raw.at("account").value("trd_market", std::string("US")));
  cfg.acc_id = raw.at("account").value("acc_id", 0);
  cfg.acc_index = raw.at("account").value("acc_index", 0);
  cfg.order_type = NormalizeExecutionOrderType(exec_raw.value("order_type", "NORMAL"));
  cfg.time_in_force = Trim(exec_raw.value("time_in_force", "DAY"));
  cfg.order_poll_min_sec = JsonToDouble(exec_raw.value("order_poll_min_sec", json(2.0)), 2.0);
  cfg.order_retry_max = exec_raw.value("order_retry_max", 3);
  cfg.order_cancel_retry_wait_sec =
      JsonToDouble(exec_raw.value("order_cancel_retry_wait_sec", json(2.0)), 2.0);
  cfg.order_limit_price_retry_wait_sec =
      JsonToDouble(exec_raw.value("order_limit_price_retry_wait_sec", json(0.5)), 0.5);
  cfg.order_too_fast_retry_wait_sec =
      JsonToDouble(exec_raw.value("order_too_fast_retry_wait_sec", json(1.0)), 1.0);
  cfg.order_rate_limit_retry_wait_sec =
      JsonToDouble(exec_raw.value("order_rate_limit_retry_wait_sec", json(2.0)), 2.0);
  cfg.order_unfilled_replace_sec =
      JsonToDouble(exec_raw.value("order_unfilled_replace_sec", json(12.0)), 12.0);
  cfg.auto_stop_after_close = exec_raw.value("auto_stop_after_close", false);
  cfg.dingding_enabled = dingding_enabled;

  const json tg_raw = notify_raw.value("telegram", json::object());
  cfg.telegram_enabled = tg_raw.value("enabled", false);
  cfg.telegram_bot_token = Trim(tg_raw.value("bot_token", std::string()));
  const json tg_paper = tg_raw.value("paper", json::object());
  const json tg_real = tg_raw.value("real", json::object());
  cfg.telegram_chat_id_paper = Trim(tg_paper.value("chat_id", std::string()));
  cfg.telegram_chat_id_real = Trim(tg_real.value("chat_id", std::string()));
  if (cfg.telegram_bot_token.empty()) {
    const char *env_tok = std::getenv("TG_BOT_TOKEN");
    if (env_tok != NULL) {
      cfg.telegram_bot_token = Trim(env_tok);
    }
  }

  const json storage_raw = raw.value("storage", json::object());
  cfg.csv_dir = Trim(storage_raw.value("csv_dir", std::string("./daily_logs")));

  if (!cfg.strategy_name.empty()) {
    // keep
  } else if (raw.contains("symbols") && raw.at("symbols").is_array() &&
             !raw.at("symbols").empty()) {
    cfg.strategy_name = Trim(raw.at("symbols").at(0).at("code").get<std::string>());
  }

  if (raw.contains("symbols") && raw.at("symbols").is_array()) {
    const json &arr = raw.at("symbols");
    if (arr.empty()) {
      throw std::runtime_error("symbols[] empty");
    }
    for (size_t i = 0; i < arr.size(); ++i) {
      cfg.symbols.push_back(ParseSymbol(arr.at(i), "symbols[" + std::to_string(i) + "]",
                                      default_order_type, default_time_in_force));
    }
  } else if (raw.contains("symbol")) {
    cfg.symbols.push_back(ParseSymbol(raw.at("symbol"), "symbol", default_order_type,
                                      default_time_in_force));
  } else {
    throw std::runtime_error("config must contain symbol or symbols[]");
  }

  EnsureTaskIds(&cfg);
  ValidateConfig(cfg);
  return cfg;
}

void ValidateConfig(const StrategyConfig &cfg) {
  if (cfg.mode != "paper" && cfg.mode != "real") {
    throw std::runtime_error("mode must be paper/real");
  }
  if (cfg.opend_port <= 0) {
    throw std::runtime_error("opend.port must be > 0");
  }
  if (cfg.order_type != "NORMAL" && cfg.order_type != "MARKET" && cfg.order_type != "SIM_MARKET") {
    throw std::runtime_error("execution.order_type invalid");
  }
  if (cfg.time_in_force != "DAY" && cfg.time_in_force != "GTC") {
    throw std::runtime_error("execution.time_in_force invalid");
  }
  if (cfg.order_retry_max < 0) {
    throw std::runtime_error("order_retry_max must be >= 0");
  }

  std::vector<std::string> codes;
  for (size_t i = 0; i < cfg.symbols.size(); ++i) {
    const SymbolConfig &sym = cfg.symbols[i];
    codes.push_back(sym.code);
    if (sym.order_type != "NORMAL" && sym.order_type != "MARKET" &&
        sym.order_type != "SIM_MARKET") {
      throw std::runtime_error(sym.code + ": order_type invalid");
    }
    if (!(sym.sim_market_pct >= 0.0 && sym.sim_market_pct < 1.0)) {
      throw std::runtime_error(sym.code + ": sim_market_pct out of range");
    }
    if (sym.time_in_force != "DAY" && sym.time_in_force != "GTC") {
      throw std::runtime_error(sym.code + ": time_in_force invalid");
    }
    if (MarketPrefix(sym.code).empty()) {
      throw std::runtime_error(sym.code + ": code must start with US. or HK.");
    }
    if (Trim(sym.task_id).empty()) {
      throw std::runtime_error(sym.code + ": task_id empty");
    }
    if (sym.entry_side != "BUY" && sym.entry_side != "SELL") {
      throw std::runtime_error(sym.code + ": entry_side invalid");
    }
    const bool use_qty = sym.entry_qty > 0;
    const bool sizing_ok_entry = use_qty || sym.budget > 0;
    const double net_long = sym.opened_qty - sym.closed_qty;
    const bool exit_managed = sym.exit.enabled && net_long > 1e-9;
    const bool cycle_done =
        sym.status == 3 || (sym.closed_qty > 1e-9 && net_long <= 1e-9);
    if (!sizing_ok_entry && !exit_managed && !cycle_done) {
      throw std::runtime_error(sym.code + ": need budget/entry_qty or exit position");
    }
    if (sym.status < 0 || sym.status > 3) {
      throw std::runtime_error(sym.code + ": status invalid");
    }
  }
  for (size_t i = 0; i < codes.size(); ++i) {
    for (size_t j = i + 1; j < codes.size(); ++j) {
      if (codes[i] == codes[j]) {
        throw std::runtime_error("symbols[].code must be unique");
      }
    }
  }
}

std::string ConfigToSummaryJson(const StrategyConfig &cfg) {
  json out;
  out["version"] = cfg.version;
  out["client_name"] = cfg.client_name;
  out["strategy_name"] = cfg.strategy_name;
  out["mode"] = cfg.mode;
  out["opend_host"] = cfg.opend_host;
  out["opend_port"] = cfg.opend_port;
  out["trd_market"] = cfg.trd_market;
  out["order_type"] = cfg.order_type;
  out["order_poll_min_sec"] = cfg.order_poll_min_sec;
  out["dingding_enabled"] = cfg.dingding_enabled;
  out["symbol_count"] = cfg.symbols.size();
  json syms = json::array();
  for (size_t i = 0; i < cfg.symbols.size(); ++i) {
    const SymbolConfig &s = cfg.symbols[i];
    json es = json::array();
    for (size_t j = 0; j < s.entry_strategies.size(); ++j) {
      const EntryStrategyRule &r = s.entry_strategies[j];
      json ct = json::array();
      for (size_t k = 0; k < r.conditions.size(); ++k) {
        ct.push_back(ConditionPythonName(r.conditions[k].kind));
      }
      es.push_back({{"id", r.rule_id},
                    {"enabled", r.enabled},
                    {"logic", r.logic},
                    {"condition_types", ct}});
    }
    json xs = json::array();
    for (size_t j = 0; j < s.exit_strategies.size(); ++j) {
      const ExitStrategyRule &r = s.exit_strategies[j];
      json ct = json::array();
      for (size_t k = 0; k < r.conditions.size(); ++k) {
        ct.push_back(ConditionPythonName(r.conditions[k].kind));
      }
      xs.push_back({{"id", r.rule_id},
                    {"enabled", r.enabled},
                    {"logic", r.logic},
                    {"side", r.side},
                    {"condition_types", ct}});
    }
    syms.push_back({{"code", s.code},
                    {"enabled", s.enabled},
                    {"entry_side", s.entry_side},
                    {"order_type", s.order_type},
                    {"time_in_force", s.time_in_force},
                    {"entry_qty", s.entry_qty > 0 ? json(s.entry_qty) : json(nullptr)},
                    {"budget", s.budget},
                    {"status", s.status},
                    {"sim_market_pct", s.sim_market_pct},
                    {"exit_enabled", s.exit.enabled},
                    {"entry_strategy_logic", s.entry_strategy_logic},
                    {"exit_strategy_logic", s.exit_strategy_logic},
                    {"entry_strategies", es},
                    {"exit_strategies", xs},
                    {"task_id_nonempty", !Trim(s.task_id).empty()}});
  }
  out["symbols"] = syms;
  return out.dump(2);
}

}  // namespace config
}  // namespace mmtrade
