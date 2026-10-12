#pragma once

#include <string>

namespace mmtrade {
namespace strategy {

double SymbolTradabilityPollSec();

struct TradabilityAssessResult {
  bool tradable;
  std::string reason;

  TradabilityAssessResult() : tradable(true) {}
};

TradabilityAssessResult AssessSymbolTradability(int sec_status, bool suspended,
                                                double last_price);

bool IsWaitableSymbolNotTradableFail(const std::string &detail);

}  // namespace strategy
}  // namespace mmtrade
