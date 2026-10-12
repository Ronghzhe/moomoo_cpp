#pragma once

#include "trade/broker_types.hpp"

#include <string>

namespace mmtrade {
namespace trade {

PlaceOrderResult PlaceResultFromGatewayHttpError(int http_code, const std::string &body);
std::string GatewayErrorText(const PlaceOrderResult &res);
bool IsGatewayInflightTimeout(const PlaceOrderResult &res);
bool IsTransientGatewayInfraFailure(const PlaceOrderResult &res);
bool ShouldFallbackToDirect(const PlaceOrderResult &res);

}  // namespace trade
}  // namespace mmtrade
