#pragma once

namespace mmtrade {
namespace market {

struct Date {
  int year;
  int month;
  int day;
};

bool operator>=(const Date &a, const Date &b);
bool operator<(const Date &a, const Date &b);

// Mirrors Python hk_tick_utils.hk_tick_size
double HkTickSize(double px, const Date *as_of);

// Mirrors Python hk_tick_utils.align_price_to_hk_tick
double AlignPriceToHkTick(double raw_px, const char *side, double ref_px, const Date *as_of);

}  // namespace market
}  // namespace mmtrade
