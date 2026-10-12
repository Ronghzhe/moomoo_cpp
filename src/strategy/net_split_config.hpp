#pragma once

#include <set>
#include <string>

namespace mmtrade {
namespace strategy {

bool EntryNetSplitEnabled();

std::set<std::string> EntryNetSplitMarkets();

std::string TradeCodeMarket(const std::string &trade_code);

bool EntryNetSplitAppliesToCode(const std::string &trade_code);

bool CppNetSplitEnabled();

bool CppNetSplitActiveForCode(const std::string &trade_code);

}  // namespace strategy
}  // namespace mmtrade
