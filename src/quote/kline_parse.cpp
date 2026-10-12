#include "quote/kline_parse.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace mmtrade {
namespace quote {

namespace {

std::string UpperCopy(const std::string &s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return out;
}

std::string TrimCopy(const std::string &s) {
  size_t start = 0;
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
    ++start;
  }
  size_t end = s.size();
  while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
    --end;
  }
  return s.substr(start, end - start);
}

}  // namespace

KlTypeKey ParseKlTypeKey(const std::string &ktype) {
  const std::string key = UpperCopy(TrimCopy(ktype));
  if (key == "K_1M") return KlTypeKey::k1M;
  if (key == "K_3M") return KlTypeKey::k3M;
  if (key == "K_5M") return KlTypeKey::k5M;
  if (key == "K_15M") return KlTypeKey::k15M;
  if (key == "K_30M") return KlTypeKey::k30M;
  if (key == "K_60M") return KlTypeKey::k60M;
  if (key == "K_DAY") return KlTypeKey::kDay;
  if (key == "K_WEEK") return KlTypeKey::kWeek;
  if (key == "K_MON") return KlTypeKey::kMon;
  if (key == "K_QUARTER") return KlTypeKey::kQuarter;
  if (key == "K_YEAR") return KlTypeKey::kYear;
  throw std::invalid_argument("unsupported ktype: " + ktype);
}

AuTypeKey ParseAuTypeKey(const std::string &autype) {
  const std::string key = UpperCopy(TrimCopy(autype));
  if (key == "QFQ") return AuTypeKey::kQfq;
  if (key == "HFQ") return AuTypeKey::kHfq;
  if (key == "NONE") return AuTypeKey::kNone;
  throw std::invalid_argument("unsupported autype: " + autype);
}

QuoteSymbol ParseQuoteSymbol(const std::string &code_full) {
  QuoteSymbol out;
  out.full = TrimCopy(code_full);
  const size_t dot = out.full.find('.');
  if (dot != std::string::npos) {
    out.prefix = UpperCopy(out.full.substr(0, dot));
    out.code = out.full.substr(dot + 1);
  } else {
    out.prefix = "US";
    out.code = out.full;
    out.full = "US." + out.code;
  }
  return out;
}

std::string MarketBucket(const std::string &code_full) {
  const QuoteSymbol sym = ParseQuoteSymbol(code_full);
  if (sym.prefix.empty()) {
    return "OTHER";
  }
  return sym.prefix;
}

std::string KlTypeFileTag(KlTypeKey key) {
  switch (key) {
    case KlTypeKey::k1M:
      return "1M";
    case KlTypeKey::k3M:
      return "3M";
    case KlTypeKey::k5M:
      return "5M";
    case KlTypeKey::k15M:
      return "15M";
    case KlTypeKey::k30M:
      return "30M";
    case KlTypeKey::k60M:
      return "60M";
    case KlTypeKey::kDay:
      return "1D";
    case KlTypeKey::kWeek:
      return "1W";
    case KlTypeKey::kMon:
      return "1MO";
    case KlTypeKey::kQuarter:
      return "1Q";
    case KlTypeKey::kYear:
      return "1Y";
  }
  return "unknown";
}

std::string SymbolFileToken(const std::string &symbol) {
  std::string t = TrimCopy(symbol);
  for (size_t i = 0; i < t.size(); ++i) {
    const char c = t[i];
    if (c == '/' || c == '\\' || c == ' ') {
      t[i] = '_';
    }
  }
  if (t.empty()) {
    return "unknown";
  }
  return t;
}

}  // namespace quote
}  // namespace mmtrade
