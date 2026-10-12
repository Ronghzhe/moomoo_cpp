#pragma once

#include <string>

namespace mmtrade {
namespace quote {

struct KlineConfig {
  std::string host;
  int port;
  std::string code;
  std::string ktype;
  std::string autype;
  int count;
  bool skip_subscribe;

  KlineConfig()
      : host("127.0.0.1"),
        port(11121),
        code("US.AAPL"),
        ktype("K_DAY"),
        autype("QFQ"),
        count(30),
        skip_subscribe(false) {}
};

}  // namespace quote
}  // namespace mmtrade
