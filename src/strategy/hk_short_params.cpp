#include "strategy/hk_short_params.hpp"

#include <cmath>
#include <sstream>

namespace mmtrade {
namespace strategy {

namespace {

std::string Upper(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    const char c = s[i];
    if (c >= 'a' && c <= 'z') {
      out.push_back(static_cast<char>(c - 'a' + 'A'));
    } else {
      out.push_back(c);
    }
  }
  return out;
}

}  // namespace

bool IsHkCode(const std::string &code) {
  const std::string u = Upper(code);
  return u.size() >= 3 && u[0] == 'H' && u[1] == 'K' && u[2] == '.';
}

bool IsHkShortOpenEntry(const std::string &code, const std::string &side,
                        const std::string &entry_side) {
  if (!IsHkCode(code)) {
    return false;
  }
  return Upper(side) == "SELL" && Upper(entry_side) == "SELL";
}

void HkShortAskBand(double ask, double *band_lo, double *band_hi, double pct) {
  const double a = ask;
  if (band_lo != NULL) {
    *band_lo = a * (1.0 - pct);
  }
  if (band_hi != NULL) {
    *band_hi = a;
  }
}

bool HkShortAskInOriginalBand(double ask, double band_lo, double band_hi) {
  if (ask <= 0.0) {
    return false;
  }
  return band_lo <= ask && ask <= band_hi + 1e-9;
}

bool HkShortBidInAskBand(double bid, double ask, double pct) {
  if (bid <= 0.0 || ask <= 0.0 || bid > ask + 1e-9) {
    return false;
  }
  double lo = 0.0;
  double hi = 0.0;
  HkShortAskBand(ask, &lo, &hi, pct);
  return lo <= bid && bid <= hi + 1e-9;
}

std::string HkShortPriceOutOfBandDetail(double ask, double band_lo, double band_hi,
                                      double ref_ask) {
  std::ostringstream oss;
  oss.setf(std::ios::fixed);
  oss.precision(6);
  oss << "ask=" << ask << " band=[" << band_lo << "," << band_hi << "] ref_ask=" << ref_ask
      << " band_pct=1%";
  return oss.str();
}

}  // namespace strategy
}  // namespace mmtrade
