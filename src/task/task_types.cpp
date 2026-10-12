#include "task/task_types.hpp"

#include <cstdio>
#include <map>
#include <sstream>
#include <stdexcept>

namespace mmtrade {
namespace task {

namespace {

int64_t ParseInt64(const std::string &v, int64_t fallback) {
  if (v.empty()) {
    return fallback;
  }
  char *end = NULL;
  const long long x = strtoll(v.c_str(), &end, 10);
  if (end == v.c_str()) {
    return fallback;
  }
  return static_cast<int64_t>(x);
}

int ParseInt(const std::string &v, int fallback) {
  return static_cast<int>(ParseInt64(v, fallback));
}

}  // namespace

TaskRow::TaskRow() : f_id(0), f_task_type(0), f_status(0) {}

PrepareClaim::PrepareClaim() : acquired(false) {}

ExecutorClaim::ExecutorClaim() : acquired(false) {}

std::string PrepareLockKey(int64_t f_id) {
  std::ostringstream oss;
  oss << "stp_" << f_id;
  return oss.str();
}

TaskRow TaskRowFromMap(const std::map<std::string, std::string> &row) {
  TaskRow out;
  std::map<std::string, std::string>::const_iterator it = row.find("f_id");
  if (it != row.end()) {
    out.f_id = ParseInt64(it->second, 0);
  }
  it = row.find("f_task_name");
  if (it != row.end()) {
    out.f_task_name = it->second;
  }
  it = row.find("f_task_type");
  if (it != row.end()) {
    out.f_task_type = ParseInt(it->second, 0);
  }
  it = row.find("f_user");
  if (it != row.end()) {
    out.f_user = it->second;
  }
  it = row.find("f_trader");
  if (it != row.end()) {
    out.f_trader = it->second;
  }
  it = row.find("f_task_params");
  if (it != row.end()) {
    out.f_task_params = it->second;
  }
  it = row.find("f_updatetime");
  if (it != row.end()) {
    out.f_updatetime = it->second;
  }
  it = row.find("f_status");
  if (it != row.end()) {
    out.f_status = ParseInt(it->second, 0);
  }
  return out;
}

}  // namespace task
}  // namespace mmtrade
