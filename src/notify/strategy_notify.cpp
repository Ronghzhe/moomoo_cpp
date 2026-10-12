#include "notify/strategy_notify.hpp"

#include "notify/telegram_client.hpp"
#include "notify/telegram_config.hpp"
#include "notify/tg_secrets_loader.hpp"

#include <cstdlib>

namespace mmtrade {
namespace notify {

namespace {

bool EnvTruthy(const char *name) {
  const char *v = std::getenv(name);
  if (v == NULL) {
    return false;
  }
  std::string s = v;
  if (s == "1" || s == "true" || s == "yes" || s == "TRUE" || s == "YES") {
    return true;
  }
  return false;
}

bool IsPaper(const config::StrategyConfig &cfg) {
  return cfg.mode == "paper";
}

TelegramBotConfig &CachedTgConfig() {
  static TelegramBotConfig cfg;
  static bool loaded = false;
  if (!loaded) {
    loaded = LoadTelegramBotConfig(DefaultTelegramConfigPath(), &cfg);
  }
  return cfg;
}

}  // namespace

bool TelegramStrategyEnabled(const config::StrategyConfig &cfg) {
  if (EnvTruthy("STRATEGY_NOTIFY_TELEGRAM")) {
    return true;
  }
  if (cfg.telegram_enabled) {
    return true;
  }
  TelegramBotConfig &tg = CachedTgConfig();
  return tg.strategy_push_enabled;
}

TelegramSetupInfo DescribeTelegramSetup(const config::StrategyConfig &cfg) {
  TelegramSetupInfo info;
  info.enabled = TelegramStrategyEnabled(cfg);
  info.config_path = DefaultTelegramConfigPath();
  info.secrets_path = DefaultTgBotSecretsPath();
  if (!info.enabled) {
    return info;
  }
  std::string token = cfg.telegram_bot_token;
  TelegramBotConfig &tg = CachedTgConfig();
  if (token.empty()) {
    token = tg.bot_token;
  }
  info.token_masked = MaskBotToken(token);
  info.chat_id = IsPaper(cfg) ? cfg.telegram_chat_id_paper : cfg.telegram_chat_id_real;
  if (info.chat_id.empty()) {
    info.chat_id = IsPaper(cfg) ? tg.paper.chat_id : tg.real.chat_id;
  }
  return info;
}

std::string ResolveTrdEnvLabel(const config::StrategyConfig &cfg) {
  return IsPaper(cfg) ? "paper" : "real";
}

NotifyResult SendStrategyNotifyText(const config::StrategyConfig &cfg, const std::string &text) {
  NotifyResult out;
  if (!TelegramStrategyEnabled(cfg)) {
    out.ok = true;
    out.detail = "disabled";
    return out;
  }
  std::string token = cfg.telegram_bot_token;
  TelegramBotConfig &tg = CachedTgConfig();
  if (token.empty()) {
    token = tg.bot_token;
  }
  if (token.empty()) {
    out.detail = "missing_bot_token";
    return out;
  }
  std::string chat_id = IsPaper(cfg) ? cfg.telegram_chat_id_paper : cfg.telegram_chat_id_real;
  if (chat_id.empty()) {
    chat_id = IsPaper(cfg) ? tg.paper.chat_id : tg.real.chat_id;
  }
  if (chat_id.empty()) {
    out.detail = "missing_chat_id";
    return out;
  }
  const TelegramSendResult sent = SendTelegramText(token, chat_id, text);
  out.ok = sent.ok;
  out.detail = sent.error;
  return out;
}

}  // namespace notify
}  // namespace mmtrade
