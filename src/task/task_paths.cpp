#include "task/task_paths.hpp"

#include <cstdlib>
#include <sstream>

namespace mmtrade {
namespace task {

namespace {

std::string EnvOr(const char *key, const std::string &fallback) {
  const char *v = std::getenv(key);
  if (v != NULL && v[0] != '\0') {
    return std::string(v);
  }
  return fallback;
}

std::string JoinPath(const std::string &a, const std::string &b) {
  if (a.empty()) {
    return b;
  }
  if (a[a.size() - 1] == '/') {
    return a + b;
  }
  return a + "/" + b;
}

}  // namespace

std::string DefaultStrategyRoot() {
  return EnvOr("MOOMOO_CPP_STRATEGY", "/home/ubuntu/workspace/moomoo_CPP/strategy");
}

std::string DefaultBackupDir() {
  return DefaultTaskBackupDir();
}

std::string DefaultTaskBackupDir() {
  const std::string env = EnvOr("MOOMOO_TASK_BACKUP_DIR", "");
  if (!env.empty()) {
    return env;
  }
  const std::string py_strategy =
      EnvOr("MOOMOO_PYTHON_STRATEGY", "/home/ubuntu/workspace/moomoo_python/strategy");
  return JoinPath(py_strategy, "config/backups");
}

std::string DefaultTaskSystemDir() {
  return JoinPath(DefaultStrategyRoot(), "task_system");
}

std::string DefaultSystemLogPath() {
  return JoinPath(DefaultTaskSystemDir(), "sys_log/system_logs_cpp.log");
}

std::string HkTemplatePath() {
  return JoinPath(DefaultStrategyRoot(), "config/strategy_config_hk.json");
}

std::string UsTemplatePath() {
  return JoinPath(DefaultStrategyRoot(), "config/strategy_config_us.json");
}

std::string RunStrategyScriptPath() {
  return EnvOr("MOOMOO_CPP_ROOT", "/home/ubuntu/workspace/moomoo_CPP") + "/run_strategy.sh";
}

std::string SafePathSegment(const std::string &name, const std::string &fallback) {
  std::string out;
  out.reserve(name.size());
  for (size_t i = 0; i < name.size(); ++i) {
    const char c = name[i];
    if (c == '/' || c == '\\' || c == ' ') {
      out.push_back('_');
    } else {
      out.push_back(c);
    }
  }
  if (out.empty()) {
    return fallback;
  }
  return out;
}

}  // namespace task
}  // namespace mmtrade
