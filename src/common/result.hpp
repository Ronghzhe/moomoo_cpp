#pragma once

#include <string>

namespace mmtrade {
namespace common {

template <typename T>
struct Result {
  int ret_code;
  T value;
  std::string error;

  Result() : ret_code(-1) {}

  bool ok() const { return ret_code == 0; }

  static Result Ok(const T &v) {
    Result r;
    r.ret_code = 0;
    r.value = v;
    return r;
  }

  static Result Fail(int code, const std::string &msg) {
    Result r;
    r.ret_code = code;
    r.error = msg;
    return r;
  }
};

template <>
struct Result<void> {
  int ret_code;
  std::string error;

  Result() : ret_code(-1) {}

  bool ok() const { return ret_code == 0; }

  static Result Ok() {
    Result r;
    r.ret_code = 0;
    return r;
  }

  static Result Fail(int code, const std::string &msg) {
    Result r;
    r.ret_code = code;
    r.error = msg;
    return r;
  }
};

}  // namespace common
}  // namespace mmtrade
