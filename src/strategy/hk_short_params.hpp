#pragma once

#include <string>

namespace mmtrade {
namespace strategy {

constexpr double kHkShortAskBandPct = 0.01;
constexpr int kHkShortMaxPlaceAttempts = 5;
constexpr int kHkNetSplitShortMaxPlaceAttempts = 10;
constexpr double kHkShortRepriceWaitSec = 3.0;
constexpr double kHkShortRepriceRateLimitPauseSec = 8.0;
constexpr double kHkShortPostCancelWaitSec = 0.35;

bool IsHkCode(const std::string &code);

bool IsHkShortOpenEntry(const std::string &code, const std::string &side,
                        const std::string &entry_side);

void HkShortAskBand(double ask, double *band_lo, double *band_hi,
                    double pct = kHkShortAskBandPct);

bool HkShortAskInOriginalBand(double ask, double band_lo, double band_hi);

bool HkShortBidInAskBand(double bid, double ask, double pct = kHkShortAskBandPct);

std::string HkShortPriceOutOfBandDetail(double ask, double band_lo, double band_hi,
                                        double ref_ask);

}  // namespace strategy
}  // namespace mmtrade
