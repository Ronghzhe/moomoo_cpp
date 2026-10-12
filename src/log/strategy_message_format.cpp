#include "log/strategy_message_format.hpp"

#include "config/strategy_types.hpp"
#include "log/ledger_ts.hpp"

#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>

namespace mmtrade {
namespace log {

namespace {

std::string TrimCopy(const std::string &s) {
  size_t start = 0;
  while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) {
    ++start;
  }
  size_t end = s.size();
  while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t')) {
    --end;
  }
  return s.substr(start, end - start);
}

// 持仓方向（与 Python strategy_logger._norm_side 一致）：long / short
std::string SideCn(const std::string &side) {
  const std::string s = TrimCopy(side);
  std::string upper;
  for (size_t i = 0; i < s.size(); ++i) {
    upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(s[i]))));
  }
  if (upper == "BUY") {
    return "long";
  }
  if (upper == "SELL") {
    return "short";
  }
  return s.empty() ? "-" : s;
}

std::string NormalizeEntrySideUpper(const std::string &entry_side) {
  const std::string s = TrimCopy(entry_side);
  std::string upper;
  upper.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(s[i]))));
  }
  return upper;
}

std::string PhaseAction(const std::string &entry_side, const char *phase) {
  const std::string upper = NormalizeEntrySideUpper(entry_side);
  if (phase == std::string("close")) {
    return upper == "BUY" ? "sell" : "buy";
  }
  return upper == "BUY" ? "buy" : "sell";
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

std::string StripLeadingBullet(const std::string &s) {
  static const std::string kBullet = "\xC2\xB7";  // UTF-8 middle dot (·)
  if (s.size() >= kBullet.size() && s.compare(0, kBullet.size(), kBullet) == 0) {
    return s.substr(kBullet.size());
  }
  return s;
}

std::string EnvLabel(const std::string &trd_env) {
  if (trd_env == "SIMULATE" || trd_env == "paper") {
    return "paper";
  }
  if (trd_env == "REAL" || trd_env == "real") {
    return "real";
  }
  return trd_env.empty() ? "paper" : trd_env;
}

bool IsHkCode(const std::string &code) {
  return code.size() >= 3 && (code.compare(0, 3, "HK.") == 0 || code.compare(0, 3, "hk.") == 0);
}

double StartupSimMarketPct(const config::SymbolConfig &sym) {
  const std::string ot = TrimCopy(sym.order_type);
  std::string upper;
  for (size_t i = 0; i < ot.size(); ++i) {
    upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ot[i]))));
  }
  if (upper == "SIM_MARKET") {
    return IsHkCode(sym.code) ? 0.01 : 0.02;
  }
  return sym.sim_market_pct > 0.0 ? sym.sim_market_pct : 0.05;
}

std::string OrderExecCnLocal(const config::SymbolConfig &sym, double sim_market_pct_override) {
  std::string ot = TrimCopy(sym.order_type);
  for (size_t i = 0; i < ot.size(); ++i) {
    ot[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(ot[i])));
  }
  if (ot == "MARKET") {
    return "市价";
  }
  if (ot == "SIM_MARKET") {
    const double pct =
        sim_market_pct_override >= 0.0 ? sim_market_pct_override : StartupSimMarketPct(sym);
    std::ostringstream pct_oss;
    pct_oss << std::fixed << std::setprecision(4) << (pct * 100.0);
    std::string pct_str = pct_oss.str();
    while (!pct_str.empty() && pct_str[pct_str.size() - 1] == '0') {
      pct_str.erase(pct_str.size() - 1, 1);
    }
    if (!pct_str.empty() && pct_str[pct_str.size() - 1] == '.') {
      pct_str.erase(pct_str.size() - 1, 1);
    }
    return pct_str + "%限仿市";
  }
  return "限价";
}

std::string FormatPriceCondLine(const config::Condition &c, const config::SymbolConfig &sym,
                                const std::string &phase) {
  const std::string op = TrimCopy(c.price.op);
  const std::string exec = OrderExecCnLocal(sym, StartupSimMarketPct(sym));
  if (op.empty()) {
    if (phase == "open") {
      return "·立即开仓 (" + exec + ")";
    }
    return "·立即平仓 (" + exec + ")";
  }
  std::ostringstream oss;
  oss << "·" << c.price.field << " " << op << " " << c.price.value << " (" << exec << ")";
  return oss.str();
}

std::string FormatConditionSummary(const config::Condition &c, const config::SymbolConfig &sym,
                                   const std::string &phase) {
  switch (c.kind) {
    case config::ConditionKind::kPrice:
      return FormatPriceCondLine(c, sym, phase);
    case config::ConditionKind::kPnl:
      return "·pnl " + c.pnl.op + " " + std::to_string(c.pnl.value);
    case config::ConditionKind::kKlineClose:
      return "·" + c.kline_close.kl_type + " close " + c.kline_close.op + " " +
             std::to_string(c.kline_close.value);
    case config::ConditionKind::kKlineCandle:
      return "·" + c.kline_candle.kl_type + " candle";
    case config::ConditionKind::kTrailingTakeProfit:
      return "·trailing_tp " + c.trailing_tp.kl_type;
    case config::ConditionKind::kTrailingMa20Close:
      return "·trailing_ma20_close " + c.trailing_ma20_close.kl_type;
    case config::ConditionKind::kTrailingMa20Realtime:
      return "·trailing_ma20_rt " + c.trailing_ma20_rt.kl_type;
    case config::ConditionKind::kStagedTakeProfit:
      return "·staged_take_profit";
    case config::ConditionKind::kPreEntryBarsLowStop:
      return "·pre_entry_low_stop " + c.pre_entry_low_stop.kl_type;
    default:
      return "·" + std::string(config::ConditionPythonName(c.kind));
  }
}

std::string FormatRuleDisplayLine(const config::SymbolConfig &sym,
                                  const std::vector<config::Condition> &conds,
                                  const std::string &logic, const std::string &phase) {
  if (conds.empty()) {
    return "·(no conditions)";
  }
  if (conds.size() == 1) {
    return FormatConditionSummary(conds[0], sym, phase);
  }
  std::ostringstream oss;
  oss << "·(" << logic << ") ";
  for (size_t i = 0; i < conds.size(); ++i) {
    if (i > 0) {
      oss << (logic == "any" ? " | " : " & ");
    }
    oss << StripLeadingBullet(FormatConditionSummary(conds[i], sym, phase));
  }
  return oss.str();
}

std::string PlanQtyLine(const config::SymbolConfig &sym, const char *phase) {
  if (phase == std::string("close")) {
    std::ostringstream oss;
    oss << "计划量: " << FmtQty(sym.opened_qty - sym.closed_qty);
    return oss.str();
  }
  if (sym.budget > 0.0) {
    std::ostringstream oss;
    oss << "计划金额: " << std::fixed << std::setprecision(0) << sym.budget;
    return oss.str();
  }
  std::ostringstream oss;
  oss << "计划量: " << FmtQty(sym.entry_qty > 0 ? sym.entry_qty : 0);
  return oss.str();
}

const config::EntryStrategyRule *FindEntryRule(const config::SymbolConfig &sym,
                                               const std::string &rule_id) {
  for (size_t i = 0; i < sym.entry_strategies.size(); ++i) {
    if (sym.entry_strategies[i].rule_id == rule_id) {
      return &sym.entry_strategies[i];
    }
  }
  return NULL;
}

const config::ExitStrategyRule *FindExitRule(const config::SymbolConfig &sym,
                                             const std::string &rule_id) {
  for (size_t i = 0; i < sym.exit_strategies.size(); ++i) {
    if (sym.exit_strategies[i].rule_id == rule_id) {
      return &sym.exit_strategies[i];
    }
  }
  return NULL;
}

void AppendEntryTriggerLines(std::ostringstream &oss, const config::SymbolConfig &sym,
                             const std::string &hit_rule_id) {
  oss << "触发开仓条件------------------------\n";
  if (!hit_rule_id.empty()) {
    oss << "·" << FormatEntryRuleHitLabel(sym, hit_rule_id) << "\n";
    return;
  }
  const std::vector<std::string> es = FormatPresetEntryLines(sym);
  for (size_t i = 0; i < es.size(); ++i) {
    oss << es[i] << "\n";
  }
}

void AppendExitTriggerLines(std::ostringstream &oss, const config::SymbolConfig &sym,
                            const std::string &exit_rule_id) {
  oss << "触发平仓条件------------------------\n";
  if (!exit_rule_id.empty()) {
    oss << "·" << FormatExitRuleHitLabel(sym, exit_rule_id) << "\n";
    return;
  }
  const std::vector<std::string> xs = FormatPresetExitLines(sym);
  for (size_t i = 0; i < xs.size(); ++i) {
    oss << xs[i] << "\n";
  }
}

}  // namespace

std::string FormatExitRuleHitLabel(const config::SymbolConfig &sym, const std::string &rule_id) {
  const config::ExitStrategyRule *rule = FindExitRule(sym, rule_id);
  if (rule == NULL || rule->conditions.empty()) {
    return rule_id.empty() ? "exit_hit" : rule_id;
  }
  return StripLeadingBullet(FormatRuleDisplayLine(sym, rule->conditions, rule->logic, "close"));
}

std::string FormatOrderExecCn(const config::SymbolConfig &sym, double sim_market_pct_override) {
  return OrderExecCnLocal(sym, sim_market_pct_override);
}

std::vector<std::string> FormatPresetEntryLines(const config::SymbolConfig &sym) {
  std::vector<std::string> out;
  bool any = false;
  for (size_t i = 0; i < sym.entry_strategies.size(); ++i) {
    const config::EntryStrategyRule &rule = sym.entry_strategies[i];
    if (!rule.enabled) {
      continue;
    }
    any = true;
    out.push_back(FormatRuleDisplayLine(sym, rule.conditions, rule.logic, "open"));
  }
  if (!any) {
    out.push_back("·无");
  }
  return out;
}

std::vector<std::string> FormatPresetExitLines(const config::SymbolConfig &sym) {
  std::vector<std::string> out;
  if (!sym.exit.enabled) {
    out.push_back("不平仓，无平仓监控");
    return out;
  }
  bool any = false;
  for (size_t i = 0; i < sym.exit_strategies.size(); ++i) {
    const config::ExitStrategyRule &rule = sym.exit_strategies[i];
    if (!rule.enabled) {
      continue;
    }
    any = true;
    out.push_back(FormatRuleDisplayLine(sym, rule.conditions, rule.logic, "close"));
  }
  if (!any) {
    out.push_back("·无");
  }
  return out;
}

std::string FormatEntryRuleHitLabel(const config::SymbolConfig &sym, const std::string &rule_id) {
  const config::EntryStrategyRule *rule = FindEntryRule(sym, rule_id);
  if (rule == NULL || rule->conditions.empty()) {
    return rule_id.empty() ? "entry_hit" : rule_id;
  }
  return StripLeadingBullet(FormatRuleDisplayLine(sym, rule->conditions, rule->logic, "open"));
}

std::string FormatMonitorStartupMessage(const config::StrategyConfig &cfg,
                                        const config::SymbolConfig &sym,
                                        const std::string &trd_env) {
  std::ostringstream oss;
  const int st = sym.status;
  const std::string now = FormatDatetimeBeijing();
  const std::string te = EnvLabel(trd_env);
  const std::string side_cn = SideCn(sym.entry_side);

  if (st == 2) {
    oss << "【监控：平仓】====" << now << "\n"
        << sym.code << "-" << side_cn << "-" << PhaseAction(sym.entry_side, "close") << "  |  "
        << PlanQtyLine(sym, "close") << "\n"
        << "s_name: " << cfg.strategy_name << "\n"
        << "持仓: 量=" << FmtQty(sym.opened_qty) << " | 价=" << std::fixed << std::setprecision(2)
        << sym.opened_price << "\n"
        << "status: " << st << "\n"
        << "env: " << te << "\n"
        << "task_id: " << sym.task_id << "\n"
        << "预设平仓条件------------------------\n";
    const std::vector<std::string> xs = FormatPresetExitLines(sym);
    for (size_t i = 0; i < xs.size(); ++i) {
      oss << xs[i] << "\n";
    }
    return oss.str();
  }

  if (st >= 3) {
    oss << "【策略本轮已结束】----------------------------------------\n"
        << sym.code << "\n"
        << "datetime: " << now << "\n"
        << "strategy_name: " << cfg.strategy_name << "\n"
        << "trd_env: " << te << "\n"
        << "status: " << st << "\n"
        << "task_id: " << sym.task_id << "\n"
        << "本轮成交: 开=" << FmtQty(sym.opened_qty) << "@" << std::fixed << std::setprecision(2)
        << sym.opened_price << " 平=" << FmtQty(sym.closed_qty) << "@" << sym.closed_price << "\n";
    if (sym.opened_price > 0.0 && sym.closed_qty > 0.0) {
      double pnl = 0.0;
      if (sym.entry_side == "BUY") {
        pnl = (sym.closed_price - sym.opened_price) * sym.closed_qty;
      } else {
        pnl = (sym.opened_price - sym.closed_price) * sym.closed_qty;
      }
      const double basis = sym.opened_price * sym.closed_qty;
      const double pctp = basis > 1e-12 ? (pnl / basis * 100.0) : 0.0;
      oss << "pnl: " << std::showpos << pnl << std::noshowpos << " (" << std::showpos << pctp
          << std::noshowpos << "%)\n";
    }
    oss << "说明: 本轮已平仓结，进程仅监控不再报单；重测请重置 status=0\n";
    return oss.str();
  }

  oss << "【监控：开仓】====" << now << "\n"
      << sym.code << "-" << side_cn << "-" << PhaseAction(sym.entry_side, "open") << "  |  "
      << PlanQtyLine(sym, "open") << "\n"
      << "s_name: " << cfg.strategy_name << "\n"
      << "env: " << te << "\n"
      << "task_id: " << sym.task_id << "\n"
      << "预设开仓条件------------------------\n";
  const std::vector<std::string> es = FormatPresetEntryLines(sym);
  for (size_t i = 0; i < es.size(); ++i) {
    oss << es[i] << "\n";
  }
  oss << "预设平仓条件------------------------\n";
  const std::vector<std::string> xs = FormatPresetExitLines(sym);
  for (size_t i = 0; i < xs.size(); ++i) {
    oss << xs[i] << "\n";
  }
  return oss.str();
}

std::string FormatEntryFillMessage(const config::StrategyConfig &cfg,
                                   const config::SymbolConfig &sym, double real_qty,
                                   double real_price, double exp_price,
                                   const std::string &order_id, const std::string &trd_env,
                                   const std::string &hit_rule_id) {
  (void)cfg;
  std::ostringstream oss;
  const double amt = real_qty * real_price;
  oss << "【开仓】====" << FormatDatetimeBeijing() << "\n"
      << sym.code << "-" << SideCn(sym.entry_side) << "-"
      << PhaseAction(sym.entry_side, "open") << "  |  " << PlanQtyLine(sym, "open") << "\n"
      << "real_price: " << std::fixed << std::setprecision(2) << real_price << " (exp="
      << exp_price << ")\n"
      << "real_qty: " << FmtQty(real_qty) << " | real_amount: " << std::setprecision(2) << amt
      << "\n"
      << "env: " << EnvLabel(trd_env) << "\n"
      << "o_status: filled (order_id:" << order_id << ")\n"
      << "task_id: " << sym.task_id << "\n";
  AppendEntryTriggerLines(oss, sym, hit_rule_id);
  oss << "预设平仓条件------------------------\n";
  const std::vector<std::string> xs = FormatPresetExitLines(sym);
  for (size_t i = 0; i < xs.size(); ++i) {
    oss << xs[i] << "\n";
  }
  return oss.str();
}

std::string FormatExitFillMessage(const config::StrategyConfig &cfg,
                                  const config::SymbolConfig &sym, const std::string &exit_side,
                                  double real_qty, double real_price, double exp_price,
                                  double entry_px, const std::string &order_id,
                                  const std::string &exit_rule_id, const std::string &trd_env) {
  (void)cfg;
  (void)exit_side;
  double pnl = 0.0;
  if (entry_px > 0.0) {
    if (sym.entry_side == "BUY") {
      pnl = (real_price - entry_px) * real_qty;
    } else {
      pnl = (entry_px - real_price) * real_qty;
    }
  }
  const double pctp = entry_px > 0.0 ? (pnl / (entry_px * real_qty)) * 100.0 : 0.0;
  std::ostringstream oss;
  oss << "【平仓】====" << FormatDatetimeBeijing() << "\n"
      << sym.code << "-" << SideCn(sym.entry_side) << "-"
      << PhaseAction(sym.entry_side, "close") << "  |  " << PlanQtyLine(sym, "close") << "\n"
      << "real_price: " << std::fixed << std::setprecision(2) << real_price << " (exp="
      << exp_price << ")\n"
      << "real_qty: " << FmtQty(real_qty) << "\n"
      << "env: " << EnvLabel(trd_env) << "\n"
      << "o_status: filled (order_id:" << order_id << ")\n"
      << "task_id: " << sym.task_id << "\n";
  AppendExitTriggerLines(oss, sym, exit_rule_id);
  oss << "【盈亏统计】------------------------\n"
      << "pnl: " << std::showpos << std::setprecision(2) << pnl << std::noshowpos << "\n"
      << "pnl%: " << std::showpos << pctp << std::noshowpos << "%\n";
  return oss.str();
}

}  // namespace log
}  // namespace mmtrade
