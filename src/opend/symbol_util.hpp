#pragma once

#include "Include_MMAPI.h"

#include <string>

namespace mmtrade {
namespace opend {

inline std::string ParseCode(const std::string &sym) {
  if (sym.find("US.") == 0) return sym.substr(3);
  if (sym.find("HK.") == 0) return sym.substr(3);
  return sym;
}

struct SdkMarketInfo {
  Trd_Common::TrdMarket trd_market;
  Trd_Common::TrdSecMarket sec_market;
  const char *name;
};

inline SdkMarketInfo ParseMarket(const std::string &sym) {
  if (sym.find("HK.") == 0) {
    return SdkMarketInfo{Trd_Common::TrdMarket_HK, Trd_Common::TrdSecMarket_HK, "HK"};
  }
  return SdkMarketInfo{Trd_Common::TrdMarket_US, Trd_Common::TrdSecMarket_US, "US"};
}

inline Trd_Common::TrdEnv TrdEnvFromSimulate(bool simulate) {
  return simulate ? Trd_Common::TrdEnv_Simulate : Trd_Common::TrdEnv_Real;
}

}  // namespace opend
}  // namespace mmtrade
