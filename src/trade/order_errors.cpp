#include "trade/order_errors.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace mmtrade {
namespace trade {

namespace {

std::string ToLowerCopy(const std::string &s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

std::string TrimCopy(const std::string &s) {
  size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) {
    ++b;
  }
  size_t e = s.size();
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
    --e;
  }
  return s.substr(b, e - b);
}

}  // namespace

bool IsOrderRateLimitErr(const std::string &detail) {
  const std::string t = ToLowerCopy(detail);
  if (t.empty()) {
    return false;
  }
  return t.find("high frequency") != std::string::npos ||
         t.find("maximum 10 times per 30 seconds") != std::string::npos ||
         t.find("maximum 10 times/30 seconds") != std::string::npos ||
         t.find("maximum 15 times per 30 seconds") != std::string::npos ||
         t.find("maximum 15 times/30 seconds") != std::string::npos ||
         t.find("too many requests") != std::string::npos ||
         t.find("rate limit") != std::string::npos || detail.find("频率") != std::string::npos ||
         detail.find("限频") != std::string::npos;
}

bool IsOperatingTooFastErr(const std::string &detail) {
  const std::string t = ToLowerCopy(detail);
  if (t.empty()) {
    return false;
  }
  return t.find("operating too fast") != std::string::npos || detail.find("操作过快") != std::string::npos;
}

bool IsCancelledAllStatus(const std::string &status) {
  return TrimCopy(ToLowerCopy(status)) == "cancelled_all";
}

bool IsCancelledAllDetail(const std::string &detail) {
  const std::string d = ToLowerCopy(TrimCopy(detail));
  return d.find("cancelled_all") != std::string::npos ||
         d.find("order_cancelled_all") != std::string::npos;
}

bool IsRetryableShortSellAskPriceFail(const std::string &detail) {
  const std::string t = ToLowerCopy(TrimCopy(detail));
  return t.find("short selling order price cannot be lower than ask") != std::string::npos;
}

bool IsRetryableLimitPriceFail(const std::string &detail) {
  const std::string d = detail;
  const std::string t = ToLowerCopy(d);
  return IsRetryableShortSellAskPriceFail(detail) ||
         t.find("limit price is much higher") != std::string::npos ||
         t.find("limit price is much lower") != std::string::npos ||
         t.find("much higher (for buys)") != std::string::npos ||
         t.find("much lower (for sells)") != std::string::npos ||
         t.find("order input price limit") != std::string::npos ||
         t.find("input price limit") != std::string::npos ||
         (d.find("限价") != std::string::npos &&
          (d.find("偏离") != std::string::npos || d.find("市价") != std::string::npos)) ||
         (d.find("输入价") != std::string::npos && d.find("限制") != std::string::npos);
}

bool IsRetryableInvalidPriceFail(const std::string &detail) {
  const std::string d = detail;
  const std::string t = ToLowerCopy(d);
  return t.find("invalid price") != std::string::npos ||
         t.find("please enter the correct price") != std::string::npos ||
         t.find("minimum tick size") != std::string::npos ||
         t.find("valid multiple") != std::string::npos ||
         (d.find("价格") != std::string::npos &&
          (d.find("无效") != std::string::npos || d.find("不正确") != std::string::npos)) ||
         (d.find("价位") != std::string::npos &&
          (d.find("无效") != std::string::npos || d.find("不合法") != std::string::npos));
}

bool IsRetryableTryAgainLater(const std::string &detail) {
  const std::string t = ToLowerCopy(TrimCopy(detail));
  if (t.empty()) {
    return false;
  }
  return t.find("please try again later") != std::string::npos ||
         detail.find("请稍后再试") != std::string::npos;
}

bool IsRetryablePlaceNoOid(const std::string &detail) {
  const std::string d = ToLowerCopy(TrimCopy(detail));
  return d.find("place_ok_but_no_oid") != std::string::npos ||
         d.find("exit_place_ok_but_no_oid") != std::string::npos || d == "no_order_id";
}

bool IsOrderFailRetriable(const std::string &status, const std::string &fail_detail) {
  return IsCancelledAllStatus(status) || IsCancelledAllDetail(fail_detail) ||
         IsRetryableLimitPriceFail(fail_detail) || IsRetryableInvalidPriceFail(fail_detail) ||
         IsOperatingTooFastErr(fail_detail) || IsOrderRateLimitErr(fail_detail) ||
         IsRetryableTryAgainLater(fail_detail) || IsRetryablePlaceNoOid(fail_detail);
}

std::string OrderFailDetailForLog(const std::string &status, const std::string &broker_err) {
  const std::string err = TrimCopy(broker_err);
  if (!err.empty()) {
    return err;
  }
  if (IsCancelledAllStatus(status)) {
    return "order_cancelled_all(last_err_msg_empty)";
  }
  const std::string st = TrimCopy(status);
  return st.empty() ? "-" : st;
}

}  // namespace trade
}  // namespace mmtrade
