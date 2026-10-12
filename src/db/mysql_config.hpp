#pragma once

#include <string>

namespace mmtrade {
namespace db {

struct MysqlConfig {
  std::string host;
  int port;
  std::string user;
  std::string password;
  std::string database;
  std::string charset;

  MysqlConfig();
};

// Load {"mysql": {...}} from mysql_config.json (same layout as Python task_system/db.py).
MysqlConfig LoadMysqlConfig(const std::string &config_path);

// strategy_trade is fixed in Python db.connect_trade_db; override only for tests.
const char *DefaultStrategyTradeDatabase();

}  // namespace db
}  // namespace mmtrade
