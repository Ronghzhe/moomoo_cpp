#include "db/mysql_client.hpp"

#include <mysql/errmsg.h>
#include <mysql/mysql.h>

#include <cstdlib>
#include <sstream>
#include <vector>

namespace mmtrade {
namespace db {

namespace {

std::string NullSafe(const char *v) { return v != NULL ? std::string(v) : std::string(); }

}  // namespace

MysqlClient::MysqlClient(const MysqlConfig &config) : config_(config), conn_(NULL) {}

MysqlClient::~MysqlClient() { Disconnect(); }

void MysqlClient::SetLastError(const std::string &msg) { last_error_ = msg; }

bool MysqlClient::IsConnectionLostError(unsigned int err) const {
  return err == CR_SERVER_GONE_ERROR || err == CR_SERVER_LOST;
}

bool MysqlClient::Connect() {
  Disconnect();
  conn_ = mysql_init(NULL);
  if (conn_ == NULL) {
    SetLastError("mysql_init failed");
    return false;
  }

  unsigned int timeout_sec = 15;
  mysql_options(conn_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout_sec);

  if (mysql_real_connect(conn_, config_.host.c_str(), config_.user.c_str(), config_.password.c_str(),
                         config_.database.c_str(), static_cast<unsigned int>(config_.port), NULL, 0) ==
      NULL) {
    SetLastError(mysql_error(conn_));
    mysql_close(conn_);
    conn_ = NULL;
    return false;
  }

  if (!config_.charset.empty()) {
    if (mysql_set_character_set(conn_, config_.charset.c_str()) != 0) {
      SetLastError(mysql_error(conn_));
      Disconnect();
      return false;
    }
  }

  if (mysql_autocommit(conn_, 1) != 0) {
    SetLastError(mysql_error(conn_));
    Disconnect();
    return false;
  }

  last_error_.clear();
  return true;
}

void MysqlClient::Disconnect() {
  if (conn_ != NULL) {
    mysql_close(conn_);
    conn_ = NULL;
  }
}

bool MysqlClient::IsConnected() const { return conn_ != NULL; }

bool MysqlClient::PingOrReconnect() {
  if (IsConnected()) {
    if (mysql_ping(conn_) == 0) {
      last_error_.clear();
      return true;
    }
    SetLastError(mysql_error(conn_));
  }
  return Connect();
}

const std::string &MysqlClient::LastError() const { return last_error_; }

bool MysqlClient::Execute(const std::string &sql) {
  if (!PingOrReconnect()) {
    return false;
  }
  if (mysql_query(conn_, sql.c_str()) != 0) {
    const unsigned int err = mysql_errno(conn_);
    SetLastError(mysql_error(conn_));
    if (IsConnectionLostError(err) && Connect() && mysql_query(conn_, sql.c_str()) == 0) {
      last_error_.clear();
      return true;
    }
    if (conn_ != NULL) {
      SetLastError(mysql_error(conn_));
    }
    return false;
  }
  last_error_.clear();
  return true;
}

bool MysqlClient::Query(const std::string &sql, MysqlRows *rows) {
  if (rows == NULL) {
    SetLastError("rows is null");
    return false;
  }
  rows->clear();
  if (!Execute(sql)) {
    return false;
  }

  MYSQL_RES *res = mysql_store_result(conn_);
  if (res == NULL) {
    if (mysql_field_count(conn_) == 0) {
      return true;
    }
    SetLastError(mysql_error(conn_));
    return false;
  }

  const unsigned int num_fields = mysql_num_fields(res);
  MYSQL_FIELD *fields = mysql_fetch_fields(res);
  MYSQL_ROW row;
  while ((row = mysql_fetch_row(res)) != NULL) {
    unsigned long *lengths = mysql_fetch_lengths(res);
    MysqlRow mapped;
    for (unsigned int i = 0; i < num_fields; ++i) {
      const char *name = fields[i].name != NULL ? fields[i].name : "";
      if (row[i] != NULL) {
        mapped[name] = std::string(row[i], lengths[i]);
      } else {
        mapped[name] = std::string();
      }
    }
    rows->push_back(mapped);
  }
  mysql_free_result(res);
  return true;
}

long long MysqlClient::AffectedRows() const {
  if (!IsConnected()) {
    return -1;
  }
  return static_cast<long long>(mysql_affected_rows(conn_));
}

std::string MysqlClient::Escape(const std::string &value) const {
  if (!IsConnected()) {
    return value;
  }
  std::vector<char> buf(value.size() * 2 + 1);
  const unsigned long n =
      mysql_real_escape_string(conn_, &buf[0], value.c_str(), static_cast<unsigned long>(value.size()));
  return std::string(&buf[0], n);
}

}  // namespace db
}  // namespace mmtrade
