#include "notify/tg_secrets_loader.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace mmtrade {
namespace notify {

namespace {

std::string Trim(const std::string &s) {
  size_t start = 0;
  while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) {
    ++start;
  }
  size_t end = s.size();
  while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r')) {
    --end;
  }
  return s.substr(start, end - start);
}

void SplitKv(const std::string &line, std::string *key_out, std::string *val_out) {
  if (key_out == NULL || val_out == NULL) {
    return;
  }
  const std::string s = Trim(line);
  if (s.empty()) {
    *key_out = std::string();
    *val_out = std::string();
    return;
  }
  const char *seps[] = {"：", ":", "="};
  for (size_t i = 0; i < sizeof(seps) / sizeof(seps[0]); ++i) {
    const std::string sep = seps[i];
    const std::size_t pos = s.find(sep);
    if (pos != std::string::npos) {
      *key_out = Trim(s.substr(0, pos));
      *val_out = Trim(s.substr(pos + sep.size()));
      return;
    }
  }
  *key_out = s;
  *val_out = std::string();
}

bool LooksLikeChatId(const std::string &value) {
  const std::string v = Trim(value);
  if (v.empty()) {
    return false;
  }
  if (v.size() >= 4 && v.compare(0, 4, "-100") == 0) {
    return true;
  }
  bool all_digits = !v.empty();
  size_t i = 0;
  if (v[0] == '-') {
    i = 1;
  }
  for (; i < v.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(v[i]))) {
      all_digits = false;
      break;
    }
  }
  return all_digits;
}

std::string LowerAscii(const std::string &s) {
  std::string out = s;
  for (size_t i = 0; i < out.size(); ++i) {
    out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
  }
  return out;
}

bool ContainsUtf8(const std::string &s, const char *needle) {
  return s.find(needle) != std::string::npos;
}

}  // namespace

TgBotSecrets::TgBotSecrets() {}

std::string DefaultTgBotSecretsPath() {
  const char *env = std::getenv("TG_BOT_SECRETS");
  if (env != NULL && env[0] != '\0') {
    return env;
  }
  const char *py = std::getenv("MOOMOO_PYTHON_ROOT");
  if (py != NULL && py[0] != '\0') {
    return std::string(py) + "/secrets/tg_bot";
  }
  return "../moomoo_python/secrets/tg_bot";
}

std::string MaskBotToken(const std::string &token) {
  if (token.empty()) {
    return "(empty)";
  }
  if (token.size() <= 8) {
    return "***";
  }
  return token.substr(0, 6) + "***";
}

bool LoadTgBotSecrets(const std::string &path, TgBotSecrets *out, std::string *error_out) {
  if (out == NULL) {
    if (error_out != NULL) {
      *error_out = "out is null";
    }
    return false;
  }
  std::ifstream in(path.c_str());
  if (!in.good()) {
    if (error_out != NULL) {
      *error_out = "secrets file not found: " + path;
    }
    return false;
  }

  *out = TgBotSecrets();
  std::string line;
  while (std::getline(in, line)) {
    std::string key;
    std::string val;
    SplitKv(line, &key, &val);
    if (key.empty()) {
      continue;
    }
    const std::string kl = LowerAscii(key);
    if (kl.find("secret") == 0) {
      out->bot_token = val;
    } else if (ContainsUtf8(key, "监控") || kl.find("monitor") != std::string::npos) {
      if (LooksLikeChatId(val)) {
        out->monitor_chat_id = val;
      } else {
        out->monitor_chat_title = val;
      }
    } else if (ContainsUtf8(key, "实盘")) {
      if (LooksLikeChatId(val)) {
        out->real_chat_id = val;
      } else {
        out->real_chat_title = val;
      }
    } else if (ContainsUtf8(key, "模拟")) {
      if (LooksLikeChatId(val)) {
        out->paper_chat_id = val;
      } else {
        out->paper_chat_title = val;
      }
    }
  }

  if (out->bot_token.empty()) {
    const char *env = std::getenv("TG_BOT_TOKEN");
    if (env != NULL && env[0] != '\0') {
      out->bot_token = Trim(env);
    }
  }
  if (out->bot_token.empty()) {
    if (error_out != NULL) {
      *error_out = "no bot token in " + path;
    }
    return false;
  }
  return true;
}

}  // namespace notify
}  // namespace mmtrade
