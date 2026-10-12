#include "trade/trade_pwd_util.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace mmtrade {
namespace trade {

namespace {

std::string Trim(const std::string &s) {
  size_t start = 0;
  while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) {
    ++start;
  }
  size_t end = s.size();
  while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t')) {
    --end;
  }
  return s.substr(start, end - start);
}

std::string RunPythonResolvePassword() {
  const char *py_root = std::getenv("MOOMOO_PYTHON_ROOT");
  std::string root = py_root != NULL ? std::string(py_root) : "../moomoo_python";
  std::ostringstream cmd;
  cmd << "python3 -c \"import sys; sys.path.insert(0,'" << root
      << "'); from tools.trade_pwd_util import resolve_trade_password; "
         "p=resolve_trade_password(); print(p or '')\" 2>/dev/null";
  FILE *pipe = popen(cmd.str().c_str(), "r");
  if (pipe == NULL) {
    return std::string();
  }
  std::array<char, 256> buf;
  std::string out;
  while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != NULL) {
    out += buf.data();
  }
  pclose(pipe);
  while (!out.empty() && (out[out.size() - 1] == '\n' || out[out.size() - 1] == '\r')) {
    out.erase(out.size() - 1, 1);
  }
  return Trim(out);
}

}  // namespace

std::string ResolveTradePasswordPlain() {
  const char *env = std::getenv("MOOMOO_TRADE_PWD");
  if (env != NULL && env[0] != '\0') {
    return Trim(env);
  }
  return RunPythonResolvePassword();
}

std::string Md5HexLower(const std::string &plain) {
  if (plain.empty()) {
    return std::string();
  }
  std::ostringstream cmd;
  cmd << "printf '%s' " << plain << " | openssl md5 2>/dev/null | awk '{print $2}'";
  // Avoid shell injection: use openssl via temp approach with escaped input
  cmd.str("");
  cmd << "echo -n ";
  for (size_t i = 0; i < plain.size(); ++i) {
    const char c = plain[i];
    if (c == '\'') {
      cmd << "'\\''";
    } else {
      cmd << c;
    }
  }
  cmd << " | openssl md5 | awk '{print $2}'";
  FILE *pipe = popen(cmd.str().c_str(), "r");
  if (pipe == NULL) {
    return std::string();
  }
  std::array<char, 64> buf;
  std::string hex;
  if (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != NULL) {
    hex = Trim(buf.data());
  }
  pclose(pipe);
  for (size_t i = 0; i < hex.size(); ++i) {
    hex[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(hex[i])));
  }
  return hex;
}

}  // namespace trade
}  // namespace mmtrade
