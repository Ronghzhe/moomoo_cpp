#pragma once

#include <string>

namespace mmtrade {
namespace notify {

struct TgBotSecrets {
  std::string bot_token;
  std::string real_chat_id;
  std::string real_chat_title;
  std::string paper_chat_id;
  std::string paper_chat_title;
  std::string monitor_chat_id;
  std::string monitor_chat_title;

  TgBotSecrets();
};

// Parse secrets/tg_bot multi-line format (aligned with Python secrets_loader.py).
bool LoadTgBotSecrets(const std::string &path, TgBotSecrets *out, std::string *error_out);

std::string DefaultTgBotSecretsPath();

std::string MaskBotToken(const std::string &token);

}  // namespace notify
}  // namespace mmtrade
