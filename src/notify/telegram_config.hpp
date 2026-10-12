#pragma once

#include <string>

namespace mmtrade {
namespace notify {

struct TelegramEndpoint {
  std::string chat_id;
  std::string chat_title;

  TelegramEndpoint() {}
};

struct TelegramBotConfig {
  std::string bot_token;
  TelegramEndpoint paper;
  TelegramEndpoint real;
  bool strategy_push_enabled;

  TelegramBotConfig() : strategy_push_enabled(false) {}
};

bool LoadTelegramBotConfig(const std::string &path, TelegramBotConfig *out);

std::string DefaultTelegramConfigPath();

}  // namespace notify
}  // namespace mmtrade
