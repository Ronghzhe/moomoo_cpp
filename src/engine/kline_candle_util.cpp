#include "engine/kline_candle_util.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace mmtrade {
namespace engine {

const char kKlineCandleRefBarOpen[] = "bar_open";
const char kKlineCandleRefPrevClose[] = "prev_close";

namespace {

const double kEps = 1e-12;

std::string LowerCopy(const std::string &s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

}  // namespace

std::string NormalizeKlineCandleRef(const std::string &raw) {
  const std::string s = LowerCopy(raw.empty() ? kKlineCandleRefPrevClose : raw);
  if (s == "bar_open" || s == "open" || s == "body" || s == "intraday" || s == "any") {
    return kKlineCandleRefBarOpen;
  }
  if (s == "prev_close" || s == "prev" || s == "true" || s == "real" || s == "yesterday") {
    return kKlineCandleRefPrevClose;
  }
  return kKlineCandleRefPrevClose;
}

KlineCandleShapeResult EvaluateKlineCandleShape(const KlineCandleCondition &cond, double close,
                                                double bar_open, double prev_close) {
  KlineCandleShapeResult out;
  const std::string cref = NormalizeKlineCandleRef(cond.candle_ref);
  out.candle_ref_norm = cref;

  double base = 0.0;
  std::string ref_label = "prev_close";
  if (cref == kKlineCandleRefBarOpen) {
    base = bar_open;
    ref_label = "bar_open";
  } else {
    base = prev_close;
  }
  out.ref_px = base;

  const int cdir = cond.candle;
  if (cref == kKlineCandleRefPrevClose && base <= kEps) {
    if (cdir == 1) {
      out.shape_rule = "真阳(假阳且close>prev_close)";
    } else if (cdir == -1) {
      out.shape_rule = "真阴(假阴且close<prev_close)";
    } else {
      out.shape_rule = "平线(close==prev_close)";
    }
    out.hit = false;
    out.cmp_note = "close vs " + ref_label + "=未就绪";
    return out;
  }

  if (cdir == 1) {
    if (cref == kKlineCandleRefPrevClose) {
      const bool fake_hit = close > bar_open + kEps;
      const bool true_hit = close > base + kEps;
      out.hit = fake_hit && true_hit;
      out.shape_rule = "真阳(假阳且close>prev_close)";
      if (fake_hit && true_hit) {
        out.cmp_note = "fake_and_true_bull";
      } else if (!fake_hit) {
        out.cmp_note = "fake_bull_failed";
      } else {
        out.cmp_note = "true_bull_failed";
      }
      return out;
    }
    out.hit = close > base + kEps;
    out.shape_rule = "阳(close>bar_open)";
  } else if (cdir == -1) {
    if (cref == kKlineCandleRefPrevClose) {
      const bool fake_hit = close < bar_open - kEps;
      const bool true_hit = close < base - kEps;
      out.hit = fake_hit && true_hit;
      out.shape_rule = "真阴(假阴且close<prev_close)";
      if (fake_hit && true_hit) {
        out.cmp_note = "fake_and_true_bear";
      } else if (!fake_hit) {
        out.cmp_note = "fake_bear_failed";
      } else {
        out.cmp_note = "true_bear_failed";
      }
      return out;
    }
    out.hit = close < base - kEps;
    out.shape_rule = "阴(close<bar_open)";
  } else {
    out.hit = std::abs(close - base) <= kEps;
    out.shape_rule = (cref == kKlineCandleRefPrevClose) ? "平线(close==prev_close)"
                                                      : "平线(close==bar_open)";
  }

  if (base <= kEps) {
    out.cmp_note = "close vs " + ref_label + "=未就绪";
  } else if (close > base + kEps) {
    out.cmp_note = "above_ref";
  } else if (close < base - kEps) {
    out.cmp_note = "below_ref";
  } else {
    out.cmp_note = "equal_ref";
  }
  return out;
}

}  // namespace engine
}  // namespace mmtrade
