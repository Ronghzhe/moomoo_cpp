#pragma once

#include "Include_MMAPI.h"

#include <google/protobuf/stubs/common.h>

#include <cstdlib>
#include <sstream>
#include <string>

namespace l1 {

inline void SdkInit() { Moomoo::MMAPI::Init(); }

inline void SdkShutdown() {
  google::protobuf::ShutdownProtobufLibrary();
  Moomoo::MMAPI::UnInit();
}

inline std::string JsonEscape(const std::string &s) {
  std::ostringstream oss;
  for (size_t i = 0; i < s.size(); ++i) {
    const char c = s[i];
    if (c == '"') {
      oss << "\\\"";
    } else if (c == '\\') {
      oss << "\\\\";
    } else {
      oss << c;
    }
  }
  return oss.str();
}

struct CommonArgs {
  std::string host;
  int port;
  std::string code;

  CommonArgs() : host("127.0.0.1"), port(11121), code("US.AAPL") {}
};

inline bool ParseCommonArgs(int argc, char **argv, CommonArgs *out) {
  if (out == NULL) {
    return false;
  }
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--host" && i + 1 < argc) {
      out->host = argv[++i];
    } else if (a == "--port" && i + 1 < argc) {
      out->port = std::atoi(argv[++i]);
    } else if (a == "--code" && i + 1 < argc) {
      out->code = argv[++i];
    }
  }
  return true;
}

}  // namespace l1
