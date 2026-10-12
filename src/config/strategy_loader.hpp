#pragma once

#include "config/strategy_types.hpp"

#include <string>

namespace mmtrade {
namespace config {

StrategyConfig LoadConfig(const std::string &path);
void ValidateConfig(const StrategyConfig &cfg);
std::string ConfigToSummaryJson(const StrategyConfig &cfg);

}  // namespace config
}  // namespace mmtrade
