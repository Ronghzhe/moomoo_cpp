#include "task/account_env.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

namespace mmtrade {
namespace task {

namespace {

const char *kLegacyFUsers[] = {"", "web", "web_trade", "test_trader"};

bool IsLegacyFUser(const std::string &f_user) {
  for (size_t i = 0; i < sizeof(kLegacyFUsers) / sizeof(kLegacyFUsers[0]); ++i) {
    if (f_user == kLegacyFUsers[i]) {
      return true;
    }
  }
  return false;
}

std::string NormalizeAccountEnv(const std::string &code, const std::string &f_env) {
  std::string e = f_env;
  for (size_t i = 0; i < e.size(); ++i) {
    e[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(e[i])));
  }
  if (e == "real" || e == "paper") {
    return e;
  }
  std::string c = code;
  for (size_t i = 0; i < c.size(); ++i) {
    c[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(c[i])));
  }
  if (c.size() >= 6 && c.substr(c.size() - 6) == "_paper") {
    return "paper";
  }
  if (c.size() >= 6 && c.compare(0, 6, "paper_") == 0) {
    return "paper";
  }
  if (c == "paper") {
    return "paper";
  }
  if (e == "simulate" || e == "sim" || e == "demo") {
    return "paper";
  }
  if (e == "live") {
    return "real";
  }
  return "paper";
}

std::string InferAccountFromTaskName(const std::string &task_name,
                                     const std::map<std::string, std::string> &env_map) {
  std::vector<std::string> codes;
  for (std::map<std::string, std::string>::const_iterator it = env_map.begin(); it != env_map.end();
       ++it) {
    codes.push_back(it->first);
  }
  std::sort(codes.begin(), codes.end(), [](const std::string &a, const std::string &b) {
    return a.size() > b.size();
  });
  for (size_t i = 0; i < codes.size(); ++i) {
    const std::string prefix = codes[i] + "_";
    if (task_name == codes[i] || (task_name.size() > prefix.size() &&
                                   task_name.compare(0, prefix.size(), prefix) == 0)) {
      return codes[i];
    }
  }
  return "";
}

}  // namespace

std::map<std::string, std::string> LoadAccountEnvMap(db::MysqlClient *db) {
  std::map<std::string, std::string> out;
  if (db == NULL || !db->IsConnected()) {
    return out;
  }
  db::MysqlRows rows;
  if (!db->Query(
          "SELECT f_code, COALESCE(NULLIF(TRIM(f_env), ''), 'paper') AS f_env "
          "FROM strategy_accounts WHERE f_status = 1",
          &rows)) {
    return out;
  }
  for (size_t i = 0; i < rows.size(); ++i) {
    const std::string code = rows[i].count("f_code") ? rows[i].at("f_code") : "";
    if (code.empty()) {
      continue;
    }
    const std::string env = rows[i].count("f_env") ? rows[i].at("f_env") : "paper";
    out[code] = NormalizeAccountEnv(code, env);
  }
  return out;
}

std::string ResolveAccountCode(const std::string &f_user, const nlohmann::json &payload,
                               const std::string &task_name, db::MysqlClient *db) {
  if (!f_user.empty() && !IsLegacyFUser(f_user)) {
    return f_user;
  }
  const std::string acct = payload.value("account", "");
  if (!acct.empty()) {
    return acct;
  }
  const std::map<std::string, std::string> env_map = LoadAccountEnvMap(db);
  return InferAccountFromTaskName(task_name, env_map);
}

std::pair<bool, std::string> ResolvePaperMode(const std::string &account, db::MysqlClient *db,
                                              bool force_simulate) {
  const std::string code = account;
  if (code.empty()) {
    throw AccountConfigError(
        "账户错误：f_user 非有效交易账户，且无法从 task_params.account / 任务名解析");
  }
  const std::map<std::string, std::string> env_map = LoadAccountEnvMap(db);
  if (env_map.empty()) {
    throw AccountConfigError("账户错误：无法加载 strategy_accounts，请检查数据库连接");
  }
  std::map<std::string, std::string>::const_iterator it = env_map.find(code);
  if (it == env_map.end()) {
    std::string known;
    size_t n = 0;
    for (std::map<std::string, std::string>::const_iterator jt = env_map.begin();
         jt != env_map.end() && n < 12; ++jt, ++n) {
      if (!known.empty()) {
        known += ", ";
      }
      known += jt->first;
    }
    throw AccountConfigError("账户错误：" + code + " 未在 strategy_accounts 中配置或未启用（f_status=1）" +
                             (known.empty() ? "" : "（已配置: " + known + "）"));
  }
  if (force_simulate) {
    return std::make_pair(true, "paper");
  }
  const std::string mode_str = (it->second == "paper") ? "paper" : "real";
  return std::make_pair(mode_str == "paper", mode_str);
}

}  // namespace task
}  // namespace mmtrade
