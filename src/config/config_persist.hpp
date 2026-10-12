#pragma once

#include "config/strategy_types.hpp"

#include <string>

namespace mmtrade {
namespace config {

void PersistStrategyConfigRuntime(const std::string &config_path, const SymbolConfig &sym);

void PersistStartupNotifyOnceConsumed(const std::string &config_path, const std::string &code);

std::string ResolveCsvDir(const std::string &config_path, const std::string &csv_dir_raw);

}  // namespace config
}  // namespace mmtrade
