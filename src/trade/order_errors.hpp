#pragma once

#include <string>

namespace mmtrade {
namespace trade {

// Mirrors strategy/order_errors.is_order_rate_limit_err.
bool IsOrderRateLimitErr(const std::string &detail);

// Mirrors strategy/order_errors.is_operating_too_fast_err.
bool IsOperatingTooFastErr(const std::string &detail);

// Mirrors strategy_main._is_cancelled_all_status / _is_cancelled_all_detail.
bool IsCancelledAllStatus(const std::string &status);
bool IsCancelledAllDetail(const std::string &detail);

// Mirrors strategy_main retryable place/fill detail classifiers.
bool IsRetryableShortSellAskPriceFail(const std::string &detail);
bool IsRetryableLimitPriceFail(const std::string &detail);
bool IsRetryableInvalidPriceFail(const std::string &detail);
bool IsRetryableTryAgainLater(const std::string &detail);
bool IsRetryablePlaceNoOid(const std::string &detail);

// Mirrors strategy_main._is_order_fail_retriable.
bool IsOrderFailRetriable(const std::string &status, const std::string &fail_detail);

// Mirrors strategy_main._order_fail_detail_for_log.
std::string OrderFailDetailForLog(const std::string &status, const std::string &broker_err);

}  // namespace trade
}  // namespace mmtrade
