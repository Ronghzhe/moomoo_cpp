#include "market/hk_tick.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <string>

namespace mmtrade {
namespace market {
namespace {

const Date kHkexSpreadPhase2Effective = {2026, 8, 3};

bool IsPhase2(const Date *as_of) {
  if (as_of == NULL) {
    return false;
  }
  return *as_of >= kHkexSpreadPhase2Effective;
}

std::string UpperTrimSide(const char *side) {
  std::string s = side ? side : "";
  size_t start = 0;
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
    ++start;
  }
  size_t end = s.size();
  while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
    --end;
  }
  s = s.substr(start, end - start);
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return s;
}

double Round6(double v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.6f", v);
  return std::atof(buf);
}

}  // namespace

bool operator>=(const Date &a, const Date &b) {
  if (a.year != b.year) return a.year > b.year;
  if (a.month != b.month) return a.month > b.month;
  return a.day >= b.day;
}

bool operator<(const Date &a, const Date &b) {
  return !(a >= b) && !(a.year == b.year && a.month == b.month && a.day == b.day);
}

double HkTickSize(double px, const Date *as_of) {
  const double p = px;
  if (p <= 0.0) {
    return 0.01;
  }
  const bool phase2 = IsPhase2(as_of);
  if (p < 0.25) return 0.001;
  if (p < 0.5) return 0.005;
  if (p < 10.0) return phase2 ? 0.005 : 0.01;
  if (p < 20.0) return 0.01;
  if (p < 50.0) return 0.02;
  if (p < 100.0) return 0.05;
  if (p < 200.0) return 0.1;
  if (p < 500.0) return 0.2;
  if (p < 1000.0) return 0.5;
  if (p < 2000.0) return 1.0;
  if (p < 5000.0) return 2.0;
  return 5.0;
}

double AlignPriceToHkTick(double raw_px, const char *side, double ref_px, const Date *as_of) {
  if (raw_px <= 0.0) {
    return raw_px;
  }
  const double tick_ref = (ref_px > 0.0) ? ref_px : raw_px;
  const double step = HkTickSize(tick_ref, as_of);
  const double n = raw_px / step;
  const std::string side_u = UpperTrimSide(side);
  double q;
  if (side_u == "BUY") {
    q = std::ceil(n - 1e-12);
  } else {
    q = std::floor(n + 1e-12);
  }
  double aligned = q * step;
  if (aligned <= 0.0) {
    return raw_px;
  }
  return Round6(aligned);
}

}  // namespace market
}  // namespace mmtrade
