#pragma once

#include "config/strategy_types.hpp"

#include <string>
#include <vector>

namespace mmtrade {
namespace log {

std::vector<std::string> FormatPresetEntryLines(const config::SymbolConfig &sym);
std::vector<std::string> FormatPresetExitLines(const config::SymbolConfig &sym);
std::string FormatEntryRuleHitLabel(const config::SymbolConfig &sym, const std::string &rule_id);
std::string FormatExitRuleHitLabel(const config::SymbolConfig &sym, const std::string &rule_id);
std::string FormatOrderExecCn(const config::SymbolConfig &sym, double sim_market_pct_override = -1.0);

std::string FormatMonitorStartupMessage(const config::StrategyConfig &cfg,
                                        const config::SymbolConfig &sym,
                                        const std::string &trd_env);

std::string FormatEntryFillMessage(const config::StrategyConfig &cfg,
                                   const config::SymbolConfig &sym, double real_qty,
                                   double real_price, double exp_price,
                                   const std::string &order_id, const std::string &trd_env,
                                   const std::string &hit_rule_id = "");

std::string FormatExitFillMessage(const config::StrategyConfig &cfg,
                                  const config::SymbolConfig &sym, const std::string &exit_side,
                                  double real_qty, double real_price, double exp_price,
                                  double entry_px, const std::string &order_id,
                                  const std::string &exit_rule_id, const std::string &trd_env);

}  // namespace log
}  // namespace mmtrade
