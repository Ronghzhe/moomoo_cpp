#include "config/config_path_resolver.hpp"

#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace mmtrade {
namespace config {

namespace {

bool IsValueFlag(const std::string &flag) {
  return flag == "-c" || flag == "--config" || flag == "--host" || flag == "--port" ||
         flag == "--poll-sec" || flag == "--max-ticks";
}

std::string TrimEnv(const char *v) {
  if (v == NULL) {
    return std::string();
  }
  std::string s = v;
  while (!s.empty() && (s[0] == ' ' || s[0] == '\t')) {
    s.erase(0, 1);
  }
  while (!s.empty()) {
    const char c = s[s.size() - 1];
    if (c != ' ' && c != '\t') {
      break;
    }
    s.erase(s.size() - 1, 1);
  }
  return s;
}

std::string AbsPathFromCwd(const std::string &raw) {
  if (raw.empty()) {
    return std::string();
  }
  if (!raw.empty() && raw[0] == '/') {
    return raw;
  }
  char cwd[4096];
  if (getcwd(cwd, sizeof(cwd)) == NULL) {
    return raw;
  }
  if (raw == ".") {
    return std::string(cwd);
  }
  return std::string(cwd) + "/" + raw;
}

std::string JoinPath(const std::string &dir, const std::string &name) {
  if (dir.empty()) {
    return name;
  }
  if (dir[dir.size() - 1] == '/') {
    return dir + name;
  }
  return dir + "/" + name;
}

}  // namespace

std::string DefaultStrategyConfigDir() {
  const char *cpp_strategy = std::getenv("MOOMOO_CPP_STRATEGY");
  if (cpp_strategy != NULL && cpp_strategy[0] != '\0') {
    return AbsPathFromCwd(cpp_strategy);
  }
  const char *cpp_root = std::getenv("MOOMOO_CPP_ROOT");
  if (cpp_root != NULL && cpp_root[0] != '\0') {
    return AbsPathFromCwd(JoinPath(cpp_root, "strategy"));
  }
  const char *env = std::getenv("MOOMOO_PYTHON_STRATEGY");
  if (env != NULL && env[0] != '\0') {
    return AbsPathFromCwd(env);
  }
  return AbsPathFromCwd("../moomoo_python/strategy");
}

ConfigPathResolveResult ResolveStrategyConfigPath(int argc, char **argv) {
  ConfigPathResolveResult out;
  bool preset_hk = false;
  bool preset_us = false;
  std::string config_opt;
  std::string positional;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "-hk" || a == "--hk") {
      preset_hk = true;
      continue;
    }
    if (a == "-us" || a == "--us") {
      preset_us = true;
      continue;
    }
    if ((a == "-c" || a == "--config") && i + 1 < argc) {
      config_opt = argv[++i];
      continue;
    }
    if (!a.empty() && a[0] == '-') {
      if (IsValueFlag(a) && i + 1 < argc) {
        ++i;
      }
      continue;
    }
    if (positional.empty()) {
      positional = a;
    }
  }

  const std::string strategy_dir = DefaultStrategyConfigDir();
  if (preset_hk) {
    out.path = AbsPathFromCwd(JoinPath(strategy_dir, "config/strategy_config_hk.json"));
    out.ok = true;
    return out;
  }
  if (preset_us) {
    out.path = AbsPathFromCwd(JoinPath(strategy_dir, "config/strategy_config_us.json"));
    out.ok = true;
    return out;
  }

  std::string raw = config_opt.empty() ? positional : config_opt;
  if (raw.empty()) {
    raw = TrimEnv(std::getenv("STRATEGY_CONFIG"));
  }
  if (raw.empty()) {
    raw = TrimEnv(std::getenv("STRATEGY_CONFIG_PATH"));
  }
  if (raw.empty()) {
    out.error =
        "未指定策略配置。请使用: -us / -hk / -c <path> / 位置参数；"
        "或 STRATEGY_CONFIG / STRATEGY_CONFIG_PATH。";
    return out;
  }

  out.path = AbsPathFromCwd(raw);
  out.ok = true;
  return out;
}

}  // namespace config
}  // namespace mmtrade
