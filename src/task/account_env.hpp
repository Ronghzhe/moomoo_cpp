#pragma once

#include "db/mysql_client.hpp"

#include <nlohmann/json.hpp>
#include <map>
#include <stdexcept>
#include <string>

namespace mmtrade {
namespace task {

class AccountConfigError : public std::runtime_error {
 public:
  explicit AccountConfigError(const std::string &msg) : std::runtime_error(msg) {}
};

std::map<std::string, std::string> LoadAccountEnvMap(db::MysqlClient *db);
std::string ResolveAccountCode(const std::string &f_user, const nlohmann::json &payload,
                               const std::string &task_name, db::MysqlClient *db);
std::pair<bool, std::string> ResolvePaperMode(const std::string &account, db::MysqlClient *db,
                                              bool force_simulate);

}  // namespace task
}  // namespace mmtrade
