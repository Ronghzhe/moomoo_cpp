#include "task/task_mapper.hpp"

#include "task/task_paths.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace mmtrade {
namespace task {

namespace {

nlohmann::json LoadTemplateJson(const std::string &stock_type) {
  const std::string path = (stock_type == "HK") ? HkTemplatePath() : UsTemplatePath();
  std::ifstream ifs(path.c_str());
  if (!ifs) {
    throw std::runtime_error("template not found: " + path);
  }
  nlohmann::json j;
  ifs >> j;
  return j;
}

std::string NormStockCode(const std::string &stock_type, std::string stock_code) {
  std::string s = stock_code;
  for (size_t i = 0; i < s.size(); ++i) {
    s[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[i])));
  }
  if (s.find('.') != std::string::npos) {
    return s;
  }
  if (stock_type == "HK") {
    while (s.size() < 5) {
      s = "0" + s;
    }
    return "HK." + s;
  }
  return "US." + s;
}

std::string KPeriodToKlType(const nlohmann::json &v) {
  std::string s = v.is_string() ? v.get<std::string>() : "15m";
  for (size_t i = 0; i < s.size(); ++i) {
    s[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
  }
  if (s.size() >= 2 && s[s.size() - 1] == 'm') {
    return "K_" + s.substr(0, s.size() - 1) + "M";
  }
  if (s.size() >= 2 && s[s.size() - 1] == 'd') {
    return "K_DAY";
  }
  return "K_15M";
}

int BarMinutesFromKlType(const std::string &kl_type) {
  if (kl_type.size() > 3 && kl_type.substr(0, 2) == "K_" && kl_type[kl_type.size() - 1] == 'M') {
    const std::string mid = kl_type.substr(2, kl_type.size() - 3);
    bool digits = !mid.empty();
    for (size_t i = 0; digits && i < mid.size(); ++i) {
      if (!std::isdigit(static_cast<unsigned char>(mid[i]))) {
        digits = false;
      }
    }
    if (digits) {
      return std::max(1, std::atoi(mid.c_str()));
    }
  }
  return 0;
}

int DefaultAfterOpenMinutes(const nlohmann::json &k_period, const std::string &kl_type) {
  const int b = BarMinutesFromKlType(kl_type);
  if (b > 0) {
    return b;
  }
  std::string s = k_period.is_string() ? k_period.get<std::string>() : "15m";
  for (size_t i = 0; i < s.size(); ++i) {
    s[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
  }
  if (s.size() >= 2 && s[s.size() - 1] == 'm') {
    return std::max(1, std::atoi(s.substr(0, s.size() - 1).c_str()));
  }
  return 15;
}

std::string NormalizeCompareOp(const nlohmann::json &raw) {
  if (raw.is_null()) {
    return "";
  }
  std::string s = raw.get<std::string>();
  for (size_t i = 0; i < s.size(); ++i) {
    s[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
  }
  if (s == "none" || s == "null") {
    return "";
  }
  return raw.get<std::string>();
}

int SafeInt(const nlohmann::json &v, int def) {
  if (v.is_null()) {
    return def;
  }
  if (v.is_number()) {
    return static_cast<int>(v.get<double>());
  }
  return def;
}

double SafeFloat(const nlohmann::json &v, double def) {
  if (v.is_null()) {
    return def;
  }
  if (v.is_number()) {
    return v.get<double>();
  }
  return def;
}

std::string SlugFromSName(std::string s_name) {
  std::string t = s_name;
  for (size_t i = 0; i < t.size(); ++i) {
    t[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(t[i])));
  }
  if (t.size() >= 2 && t[0] == 's' && t[1] == '_') {
    t = t.substr(2);
  }
  std::string slug;
  for (size_t i = 0; i < t.size(); ++i) {
    const char c = t[i];
    if (std::isalnum(static_cast<unsigned char>(c))) {
      slug.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    } else {
      slug.push_back('_');
    }
  }
  while (slug.find("__") != std::string::npos) {
    slug.replace(slug.find("__"), 2, "_");
  }
  if (slug.empty()) {
    slug = "COND";
  }
  return slug;
}

std::string EntryRuleId(int seq, const nlohmann::json &raw) {
  const std::string s_name = raw.value("s_name", "");
  if (s_name.empty()) {
    return "S" + std::to_string(seq) + "_COND";
  }
  return "S" + std::to_string(seq) + "_" + SlugFromSName(s_name);
}

std::string ExitRuleId(int seq, const nlohmann::json &raw) {
  const std::string s_name = raw.value("s_name", "");
  if (s_name.empty()) {
    return "X" + std::to_string(seq) + "_COND";
  }
  return "X" + std::to_string(seq) + "_" + SlugFromSName(s_name);
}

nlohmann::json MapOrderbookParams(const nlohmann::json &p, const std::string &fallback_field) {
  std::string field = p.value("field", "");
  if (field.empty()) {
    field = fallback_field;
  }
  const std::string op = NormalizeCompareOp(p.contains("op") ? p.at("op") : nlohmann::json());
  nlohmann::json val = nullptr;
  if (p.contains("value") && !p.at("value").is_null()) {
    const nlohmann::json &raw_v = p.at("value");
    if (!(raw_v.is_string() && raw_v.get<std::string>().empty())) {
      val = SafeFloat(raw_v, 0.0);
    }
  }
  return nlohmann::json{{"type", "orderbook"}, {"field", field}, {"op", op}, {"value", val}};
}

nlohmann::json MapEntryCond(const nlohmann::json &c, const std::string &side) {
  const std::string s_name = c.value("s_name", "");
  const nlohmann::json p = c.value("params", nlohmann::json::object());
  if (s_name == "entry_now" || s_name == "trade_now") {
    const std::string field = (side == "long") ? "ask_1" : "bid_1";
    return nlohmann::json{{"type", "orderbook"}, {"field", field}, {"op", ""}, {"value", nullptr}};
  }
  if (s_name == "orderbook" || s_name == "s_orderbook") {
    const std::string fb = (side == "long") ? "ask_1" : "bid_1";
    return MapOrderbookParams(p, fb);
  }
  if (s_name == "s_check_open") {
    return nlohmann::json{{"type", "open"},
                          {"gain_pct", SafeFloat(p.value("gain_pct", 0.0), 0.0)},
                          {"ref", p.value("ref", "mid")}};
  }
  if (s_name == "s_check_volume") {
    const std::string kl_type = KPeriodToKlType(p.value("k_period", "15m"));
    return nlohmann::json{{"type", "volume"},
                          {"kl_type", kl_type},
                          {"ma_period", SafeInt(p.value("ma_period", 20), 20)},
                          {"multiple", SafeFloat(p.value("multiple", 2.0), 2.0)},
                          {"ma_include_last", p.value("ma_include_last", true)},
                          {"poll_sec", std::max(1.0, SafeFloat(p.value("poll_sec", 30.0), 30.0))}};
  }
  if (s_name == "s_check_kline_close") {
    const std::string kl_type = KPeriodToKlType(p.value("k_period", "15m"));
    const std::string op = NormalizeCompareOp(p.contains("op") ? p.at("op") : nlohmann::json());
    nlohmann::json val = nullptr;
    if (!op.empty() && p.contains("value") && !p.at("value").is_null()) {
      val = SafeFloat(p.at("value"), 0.0);
    }
    return nlohmann::json{{"type", "kline_close"},
                          {"kl_type", kl_type},
                          {"op", op},
                          {"value", val},
                          {"after_open_minutes", std::max(0, SafeInt(p.value("after_open_minutes", 0), 0))},
                          {"poll_sec", std::max(1.0, SafeFloat(p.value("poll_sec", 30.0), 30.0))}};
  }
  if (s_name == "s_check_open_side") {
    const std::string check_side = p.value("check_side", "up");
    const int candle = (check_side == "up") ? 1 : -1;
    const std::string kl_type = KPeriodToKlType(p.value("k_period", "15m"));
    int after_open = DefaultAfterOpenMinutes(p.value("k_period", "15m"), kl_type);
    if (p.contains("after_open_minutes") && !p.at("after_open_minutes").is_null()) {
      after_open = SafeInt(p.at("after_open_minutes"), after_open);
    }
    std::string candle_ref = "prev_close";
    if (p.contains("candle_ref") && !p.at("candle_ref").is_null()) {
      candle_ref = p.value("candle_ref", "bar_open");
    } else {
      const std::string yang_ref = p.value("yang_ref", p.value("candle_compare", "true"));
      std::string yr = yang_ref;
      for (size_t i = 0; i < yr.size(); ++i) {
        yr[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(yr[i])));
      }
      if (yr == "body" || yr == "open" || yr == "bar_open" || yr == "any" || yr == "intraday") {
        candle_ref = "bar_open";
      }
    }
    return nlohmann::json{{"type", "kline_candle"},
                          {"kl_type", kl_type},
                          {"candle", candle},
                          {"candle_ref", candle_ref},
                          {"after_open_minutes", std::max(0, after_open)},
                          {"poll_sec", std::max(1, SafeInt(p.value("poll_sec", 10), 10))},
                          {"eval_mode", p.value("eval_mode", "once")}};
  }
  return nlohmann::json();
}

nlohmann::json MapExitCond(const nlohmann::json &c, const std::string &side) {
  const std::string s_name = c.value("s_name", "");
  const nlohmann::json p = c.value("params", nlohmann::json::object());
  if (s_name == "exit_now" || s_name == "trade_now") {
    const std::string field = (side == "long") ? "bid_1" : "ask_1";
    return nlohmann::json{{"type", "orderbook"}, {"field", field}, {"op", ""}, {"value", nullptr}};
  }
  if (s_name == "orderbook" || s_name == "s_orderbook") {
    const std::string fb = (side == "long") ? "bid_1" : "ask_1";
    return MapOrderbookParams(p, fb);
  }
  if (s_name == "s_ma_k_cross") {
    const std::string cross = p.value("cross_side", "down");
    const std::string op = (cross == "down") ? "<" : ">";
    nlohmann::json row = {{"type", "trailing_ma20_close"},
                          {"kl_type", KPeriodToKlType(p.value("k_period", "15m"))},
                          {"ma_period", SafeInt(p.value("ma_period", p.value("ma_n", 20)), 20)},
                          {"op", op},
                          {"poll_sec", std::max(1.0, SafeFloat(p.value("poll_sec", 10.0), 10.0))},
                          {"log_interval_sec", std::max(1.0, SafeFloat(p.value("log_interval_sec", 20.0), 20.0))}};
    const double relax = SafeFloat(p.value("ma_ref_relax", 0.0), 0.0);
    if (std::fabs(relax) > 1e-15) {
      row["ma_ref_relax"] = relax;
    }
    return row;
  }
  if (s_name == "s_trailing_ma_close") {
    nlohmann::json row = {{"type", "trailing_ma20_close"},
                          {"kl_type", KPeriodToKlType(p.value("k_period", "15m"))},
                          {"ma_period", SafeInt(p.value("ma_period", p.value("ma_n", 20)), 20)},
                          {"op", NormalizeCompareOp(p.contains("op") ? p.at("op") : nlohmann::json())},
                          {"poll_sec", std::max(1.0, SafeFloat(p.value("poll_sec", 10.0), 10.0))},
                          {"log_interval_sec", std::max(1.0, SafeFloat(p.value("log_interval_sec", 20.0), 20.0))}};
    const double relax = SafeFloat(p.value("ma_ref_relax", 0.0), 0.0);
    if (std::fabs(relax) > 1e-15) {
      row["ma_ref_relax"] = relax;
    }
    return row;
  }
  if (s_name == "s_trailing_ma_realtime" || s_name == "s_trailing_ma_realtme") {
    std::string pf = p.value("price_field", p.value("field", "bid_1"));
    for (size_t i = 0; i < pf.size(); ++i) {
      pf[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(pf[i])));
    }
    return nlohmann::json{{"type", "trailing_ma20_realtime"},
                          {"kl_type", KPeriodToKlType(p.value("k_period", "15m"))},
                          {"ma_period", SafeInt(p.value("ma_period", p.value("ma_n", 20)), 20)},
                          {"op", NormalizeCompareOp(p.contains("op") ? p.at("op") : nlohmann::json())},
                          {"price_field", pf},
                          {"poll_sec", std::max(1.0, SafeFloat(p.value("poll_sec", 3.0), 3.0))},
                          {"log_interval_sec", std::max(1.0, SafeFloat(p.value("log_interval_sec", 20.0), 20.0))}};
  }
  if (s_name == "s_ma_tick_cross") {
    const std::string cross = p.value("cross_side", "down");
    const std::string op = (cross == "down") ? "<" : ">";
    std::string pf = p.value("price_field", "bid_1");
    for (size_t i = 0; i < pf.size(); ++i) {
      pf[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(pf[i])));
    }
    return nlohmann::json{{"type", "trailing_ma20_realtime"},
                          {"kl_type", KPeriodToKlType(p.value("k_period", "15m"))},
                          {"ma_period", SafeInt(p.value("ma_period", p.value("ma_n", 20)), 20)},
                          {"op", op},
                          {"price_field", pf},
                          {"poll_sec", std::max(1.0, SafeFloat(p.value("poll_sec", 3.0), 3.0))},
                          {"log_interval_sec", std::max(1.0, SafeFloat(p.value("log_interval_sec", 20.0), 20.0))}};
  }
  if (s_name == "s_partial_exit" || s_name == "s_staged_take_profit") {
    std::string pf = p.value("price_field", p.value("field", "bid_1"));
    nlohmann::json tg = p.contains("targets") ? p.at("targets") : p.value("win_list", nlohmann::json::array());
    nlohmann::json fr = p.contains("fractions") ? p.at("fractions") : p.value("exit_r_list", nlohmann::json::array());
    return nlohmann::json{{"type", "staged_take_profit"}, {"price_field", pf}, {"targets", tg}, {"fractions", fr}};
  }
  if (s_name == "s_kline_pre_entry_low_stop") {
    return nlohmann::json{{"type", "kline_pre_entry_low_stop"},
                          {"kl_type", KPeriodToKlType(p.value("k_period", "15m"))},
                          {"bars", std::max(1, SafeInt(p.value("bars", 2), 2))},
                          {"low_offset", SafeFloat(p.value("low_offset", 0.005), 0.005)},
                          {"price_field", p.value("price_field", "bid_1")},
                          {"poll_sec", std::max(1.0, SafeFloat(p.value("poll_sec", 30.0), 30.0))},
                          {"log_interval_sec", std::max(1.0, SafeFloat(p.value("log_interval_sec", 20.0), 20.0))}};
  }
  if (s_name == "s_before_session_close") {
    return nlohmann::json{{"type", "before_session_close"},
                          {"minutes_before", std::max(1, SafeInt(p.value("minutes_before", 1), 1))},
                          {"session", p.value("session", "rth")},
                          {"poll_sec", std::max(1.0, SafeFloat(p.value("poll_sec", 30.0), 30.0))}};
  }
  return nlohmann::json();
}

bool TaskSymAlwaysEnabled(const std::string &task_type) {
  return task_type == "monitor_entry" || task_type == "monitor_exit" || task_type == "monitor_all" ||
         task_type == "entry_now" || task_type == "exit_now";
}

}  // namespace

MappedConfigResult BuildStrategyConfig(const std::string &task_name, const nlohmann::json &params,
                                       bool paper_mode) {
  const std::string stock_type = params.value("stock_type", "HK");
  nlohmann::json cfg = LoadTemplateJson(stock_type);

  cfg["mode"] = paper_mode ? "paper" : "real";
  const std::string raw_strategy_name = params.value("strategy_name", "");
  if (!raw_strategy_name.empty()) {
    cfg["strategy_name"] = raw_strategy_name;
  }

  std::string side = params.value("side", "long");
  for (size_t i = 0; i < side.size(); ++i) {
    side[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(side[i])));
  }
  const std::string task_type = params.value("task_type", "");
  if (task_type.empty()) {
    throw std::runtime_error("task_type is required and must be non-empty");
  }
  std::string order_type = params.value("order_type", "market");
  for (size_t i = 0; i < order_type.size(); ++i) {
    order_type[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(order_type[i])));
  }
  const std::string stock_code = NormStockCode(stock_type, params.value("stock_code", ""));

  if (!cfg.contains("symbols") || !cfg["symbols"].is_array() || cfg["symbols"].empty()) {
    cfg["symbols"] = nlohmann::json::array({nlohmann::json::object()});
  }
  nlohmann::json &sym = cfg["symbols"][0];

  sym["task_id"] = task_name;
  sym["code"] = stock_code;
  sym["instrument"] = nlohmann::json{{"type", "stock"}};
  sym["startup_notify_once"] = true;
  sym["entry_side"] = (side == "long") ? "BUY" : "SELL";
  sym["order_type"] = (order_type == "market") ? "sim_market" : "NORMAL";

  if (params.contains("sim_market_pct") && !params.at("sim_market_pct").is_null()) {
    sym["sim_market_pct"] = SafeFloat(params.at("sim_market_pct"), 0.03);
  } else {
    sym["sim_market_pct"] = 0.03;
  }

  const nlohmann::json &qty = params.at("trade_qty");
  const nlohmann::json &amount = params.at("trade_amount");
  if (task_type == "monitor_exit" || task_type == "exit_now") {
    sym["entry_qty"] = nullptr;
    sym["exit_qty"] = qty.is_null() ? nullptr : nlohmann::json(SafeFloat(qty, 0.0));
    sym["budget"] = amount.is_null() ? nullptr : nlohmann::json(SafeFloat(amount, 0.0));
    sym["initial_qty"] = nullptr;
  } else {
    sym["entry_qty"] = qty.is_null() ? nullptr : nlohmann::json(SafeFloat(qty, 0.0));
    sym["budget"] = amount.is_null() ? nullptr : nlohmann::json(SafeFloat(amount, 0.0));
    sym["initial_qty"] = sym["entry_qty"].is_null() ? nlohmann::json(0.0) : sym["entry_qty"];
  }
  sym["opened_qty"] = 0.0;
  sym["opened_price"] = 0.0;
  sym["closed_qty"] = 0.0;
  sym["closed_price"] = 0.0;

  struct Row {
    nlohmann::json raw;
    nlohmann::json mapped;
  };
  std::vector<Row> entry_rows;
  if (params.contains("entry_conditions") && params.at("entry_conditions").is_array()) {
    for (size_t i = 0; i < params.at("entry_conditions").size(); ++i) {
      const nlohmann::json &c = params.at("entry_conditions").at(i);
      if (!c.is_object()) {
        continue;
      }
      const nlohmann::json m = MapEntryCond(c, side);
      if (!m.is_null() && !m.empty()) {
        Row row;
        row.raw = c;
        row.mapped = m;
        entry_rows.push_back(row);
      }
    }
  }
  if (task_type == "entry_now" && entry_rows.empty()) {
    Row row;
    row.raw = nlohmann::json{{"s_name", "entry_now"}, {"params", nlohmann::json::object()}};
    const std::string field = (side == "long") ? "ask_1" : "bid_1";
    row.mapped = nlohmann::json{{"type", "orderbook"}, {"field", field}, {"op", ""}, {"value", nullptr}};
    entry_rows.push_back(row);
  }

  nlohmann::json entry_strategies = nlohmann::json::array();
  for (size_t i = 0; i < entry_rows.size(); ++i) {
    entry_strategies.push_back(nlohmann::json{{"id", EntryRuleId(static_cast<int>(i + 1), entry_rows[i].raw)},
                                              {"enabled", true},
                                              {"logic", "all"},
                                              {"conditions", nlohmann::json::array({entry_rows[i].mapped})}});
  }
  sym["entry_strategies"] = entry_strategies;
  sym["entry_strategy_logic"] = "any";

  std::vector<Row> exit_rows;
  if (task_type == "exit_now") {
    Row row;
    row.raw = nlohmann::json{{"s_name", "exit_now"}, {"params", nlohmann::json::object()}};
    row.mapped = MapExitCond(row.raw, side);
    exit_rows.push_back(row);
  } else if (params.contains("exit_conditions") && params.at("exit_conditions").is_array()) {
    for (size_t i = 0; i < params.at("exit_conditions").size(); ++i) {
      const nlohmann::json &c = params.at("exit_conditions").at(i);
      if (!c.is_object()) {
        continue;
      }
      const nlohmann::json m = MapExitCond(c, side);
      if (!m.is_null() && !m.empty()) {
        Row row;
        row.raw = c;
        row.mapped = m;
        exit_rows.push_back(row);
      }
    }
  }
  const std::string exit_side = (side == "long") ? "SELL" : "BUY";
  nlohmann::json exit_strategies = nlohmann::json::array();
  for (size_t i = 0; i < exit_rows.size(); ++i) {
    exit_strategies.push_back(nlohmann::json{{"id", ExitRuleId(static_cast<int>(i + 1), exit_rows[i].raw)},
                                             {"enabled", true},
                                             {"side", exit_side},
                                             {"logic", "all"},
                                             {"conditions", nlohmann::json::array({exit_rows[i].mapped})}});
  }
  sym["exit_strategies"] = exit_strategies;
  sym["exit_strategy_logic"] = "any";
  if (task_type == "entry_now") {
    sym["exit_strategies"] = nlohmann::json::array();
  }

  sym["enabled"] = TaskSymAlwaysEnabled(task_type);
  if (task_type == "monitor_entry" || task_type == "entry_now") {
    sym["exit_enabled"] = false;
  } else if (task_type == "exit_now" || task_type == "monitor_exit" || task_type == "monitor_all") {
    sym["exit_enabled"] = true;
  } else {
    sym["exit_enabled"] = !exit_rows.empty();
  }
  if (task_type == "exit_now" || task_type == "monitor_exit") {
    sym["status"] = 2;
  } else {
    sym["status"] = 0;
  }

  if (!cfg.contains("account") || !cfg["account"].is_object()) {
    cfg["account"] = nlohmann::json::object();
  }
  cfg["account"]["trd_market"] = stock_type;

  MappedConfigResult out;
  out.config = cfg;
  out.summary = nlohmann::json{{"task_type", task_type},
                               {"stock_type", stock_type},
                               {"code", stock_code},
                               {"mode", cfg["mode"]},
                               {"entry_side", sym["entry_side"]},
                               {"order_type", sym["order_type"]},
                               {"enabled", sym["enabled"]},
                               {"exit_enabled", sym["exit_enabled"]},
                               {"entry_conditions_count", entry_rows.size()},
                               {"exit_conditions_count", exit_rows.size()}};
  return out;
}

}  // namespace task
}  // namespace mmtrade
