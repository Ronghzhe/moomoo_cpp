#pragma once

#include "db/mysql_config.hpp"

#include <map>
#include <string>
#include <vector>

struct MYSQL;

namespace mmtrade {
namespace db {

typedef std::map<std::string, std::string> MysqlRow;
typedef std::vector<MysqlRow> MysqlRows;

class MysqlClient {
 public:
  explicit MysqlClient(const MysqlConfig &config);
  ~MysqlClient();

  bool Connect();
  void Disconnect();
  bool IsConnected() const;
  /** Ping server; reconnect if connection is dead. */
  bool PingOrReconnect();
  const std::string &LastError() const;

  bool Execute(const std::string &sql);
  bool Query(const std::string &sql, MysqlRows *rows);
  long long AffectedRows() const;

  std::string Escape(const std::string &value) const;

 private:
  MysqlClient(const MysqlClient &);
  MysqlClient &operator=(const MysqlClient &);

  void SetLastError(const std::string &msg);
  bool IsConnectionLostError(unsigned int err) const;

  MysqlConfig config_;
  MYSQL *conn_;
  std::string last_error_;
};

}  // namespace db
}  // namespace mmtrade
