#pragma once

#include <string>

namespace mmtrade {
namespace quote {

enum class KlTypeKey {
  k1M,
  k3M,
  k5M,
  k15M,
  k30M,
  k60M,
  kDay,
  kWeek,
  kMon,
  kQuarter,
  kYear,
};

enum class AuTypeKey { kQfq, kHfq, kNone };

struct QuoteSymbol {
  std::string prefix;
  std::string code;
  std::string full;
};

KlTypeKey ParseKlTypeKey(const std::string &ktype);
AuTypeKey ParseAuTypeKey(const std::string &autype);
QuoteSymbol ParseQuoteSymbol(const std::string &code_full);
std::string MarketBucket(const std::string &code_full);
std::string KlTypeFileTag(KlTypeKey key);
std::string SymbolFileToken(const std::string &symbol);

}  // namespace quote
}  // namespace mmtrade
