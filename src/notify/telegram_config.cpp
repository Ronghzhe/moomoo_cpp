#include "notify/telegram_config.hpp"

#include "notify/tg_secrets_loader.hpp"

#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>

namespace mmtrade {
namespace notify {

namespace {

using json = nlohmann::json;

std::string Trim(const std::string &s) {
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

std::string ReadTokenFile(const std::string &path) {
  std::ifstream in(path.c_str());
  if (!in.good()) {
    return std::string();
  }
  std::string line;
  std::getline(in, line);
  return Trim(line);
}

TelegramEndpoint ParseEndpoint(const json &raw) {
  TelegramEndpoint ep;
  if (!raw.is_object()) {
    return ep;
  }
  ep.chat_id = Trim(raw.value("chat_id", std::string()));
  ep.chat_title = Trim(raw.value("chat_title", std::string()));
  return ep;
}

}  // namespace

std::string DefaultTelegramConfigPath() {
  const char *env = std::getenv("TG_BOT_CONFIG");
  if (env != NULL && env[0] != '\0') {
    return env;
  }
  const char *py = std::getenv("MOOMOO_PYTHON_ROOT");
  if (py != NULL && py[0] != '\0') {
    return std::string(py) + "/telegram/telegram_config.json";
  }
  return "../moomoo_python/telegram/telegram_config.json";
}

bool LoadTelegramBotConfig(const std::string &path, TelegramBotConfig *out) {
  if (out == NULL) {
    return false;
  }
  std::ifstream ifs(path.c_str());
  if (!ifs.good()) {
    return false;
  }
  json raw;
  ifs >> raw;
  out->bot_token = Trim(raw.value("bot_token", std::string()));
  const std::string token_file = Trim(raw.value("bot_token_file", std::string()));
  if (out->bot_token.empty() && !token_file.empty()) {
    std::string resolved = token_file;
    if (!resolved.empty() && resolved[0] != '/') {
      const std::size_t slash = path.find_last_of('/');
      if (slash != std::string::npos) {
        resolved = path.substr(0, slash + 1) + resolved;
      }
    }
    TgBotSecrets secrets;
    std::string sec_err;
    if (LoadTgBotSecrets(resolved, &secrets, &sec_err)) {
      out->bot_token = secrets.bot_token;
      if (out->paper.chat_id.empty() && !secrets.paper_chat_id.empty()) {
        out->paper.chat_id = secrets.paper_chat_id;
        out->paper.chat_title = secrets.paper_chat_title;
      }
      if (out->real.chat_id.empty() && !secrets.real_chat_id.empty()) {
        out->real.chat_id = secrets.real_chat_id;
        out->real.chat_title = secrets.real_chat_title;
      }
    }
  }
  if (out->bot_token.empty()) {
    TgBotSecrets secrets;
    std::string sec_err;
    if (LoadTgBotSecrets(DefaultTgBotSecretsPath(), &secrets, &sec_err)) {
      out->bot_token = secrets.bot_token;
      if (out->paper.chat_id.empty() && !secrets.paper_chat_id.empty()) {
        out->paper.chat_id = secrets.paper_chat_id;
      }
      if (out->real.chat_id.empty() && !secrets.real_chat_id.empty()) {
        out->real.chat_id = secrets.real_chat_id;
      }
    }
  }
  if (out->bot_token.empty()) {
    const char *env = std::getenv("TG_BOT_TOKEN");
    if (env != NULL) {
      out->bot_token = Trim(env);
    }
  }
  out->paper = ParseEndpoint(raw.value("paper", json::object()));
  out->real = ParseEndpoint(raw.value("real", json::object()));
  const json push = raw.value("strategy_push", json::object());
  out->strategy_push_enabled = push.value("enabled", false);
  return !out->bot_token.empty();
}

}  // namespace notify
}  // namespace mmtrade
