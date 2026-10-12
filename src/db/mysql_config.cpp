#include "db/mysql_config.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace mmtrade {
namespace db {

namespace {

std::string ReadFileText(const std::string &path) {
  std::ifstream in(path.c_str());
  if (!in.is_open()) {
    throw std::runtime_error("mysql config file not found: " + path);
  }
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

MysqlConfig ParseMysqlSection(const nlohmann::json &cfg) {
  if (!cfg.is_object()) {
    throw std::runtime_error("mysql config section must be an object");
  }
  MysqlConfig out;
  if (!cfg.contains("host") || !cfg["host"].is_string()) {
    throw std::runtime_error("mysql.host missing");
  }
  out.host = cfg["host"].get<std::string>();
  out.port = cfg.contains("port") ? cfg["port"].get<int>() : 3306;
  if (!cfg.contains("user") || !cfg["user"].is_string()) {
    throw std::runtime_error("mysql.user missing");
  }
  out.user = cfg["user"].get<std::string>();
  if (!cfg.contains("password") || !cfg["password"].is_string()) {
    throw std::runtime_error("mysql.password missing");
  }
  out.password = cfg["password"].get<std::string>();
  out.database = DefaultStrategyTradeDatabase();
  if (cfg.contains("database") && cfg["database"].is_string()) {
    const std::string db = cfg["database"].get<std::string>();
    if (!db.empty()) {
      out.database = db;
    }
  }
  out.charset = cfg.contains("charset") && cfg["charset"].is_string() ? cfg["charset"].get<std::string>()
                                                                      : "utf8mb4";
  return out;
}

}  // namespace

MysqlConfig::MysqlConfig() : port(3306), database(DefaultStrategyTradeDatabase()), charset("utf8mb4") {}

const char *DefaultStrategyTradeDatabase() { return "strategy_trade"; }

MysqlConfig LoadMysqlConfig(const std::string &config_path) {
  if (config_path.empty()) {
    throw std::runtime_error("mysql config path is empty");
  }
  const nlohmann::json root = nlohmann::json::parse(ReadFileText(config_path));
  if (!root.contains("mysql")) {
    throw std::runtime_error("mysql config invalid: missing mysql section");
  }
  return ParseMysqlSection(root["mysql"]);
}

}  // namespace db
}  // namespace mmtrade
