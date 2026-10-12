#pragma once

#include <string>

namespace mmtrade {
namespace config {

struct ConfigPathResolveResult {
  std::string path;
  bool ok;
  std::string error;

  ConfigPathResolveResult() : ok(false) {}
};

// Mirrors strategy_main._resolve_strategy_config_path (hk/us/-c/positional/env).
ConfigPathResolveResult ResolveStrategyConfigPath(int argc, char **argv);

std::string DefaultStrategyConfigDir();

}  // namespace config
}  // namespace mmtrade
