#pragma once

#include "config/strategy_types.hpp"

#include <string>

namespace mmtrade {
namespace notify {

struct NotifyResult {
  bool ok;
  std::string detail;

  NotifyResult() : ok(false) {}
};

struct TelegramSetupInfo {
  bool enabled;
  std::string token_masked;
  std::string chat_id;
  std::string config_path;
  std::string secrets_path;

  TelegramSetupInfo() : enabled(false) {}
};

bool TelegramStrategyEnabled(const config::StrategyConfig &cfg);

TelegramSetupInfo DescribeTelegramSetup(const config::StrategyConfig &cfg);

NotifyResult SendStrategyNotifyText(const config::StrategyConfig &cfg, const std::string &text);

std::string ResolveTrdEnvLabel(const config::StrategyConfig &cfg);

}  // namespace notify
}  // namespace mmtrade
