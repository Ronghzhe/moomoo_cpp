#pragma once

#include <string>

namespace mmtrade {
namespace trade {

// Mirrors Python tools.trade_pwd_util.resolve_trade_password (MOOMOO_TRADE_PWD or Fernet files).
std::string ResolveTradePasswordPlain();

std::string Md5HexLower(const std::string &plain);

}  // namespace trade
}  // namespace mmtrade
