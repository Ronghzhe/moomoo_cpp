#pragma once

#include "opend/direct_broker.hpp"
#include "trade/broker_types.hpp"
#include "trade/fallback_broker.hpp"
#include "trade/gateway_broker.hpp"
#include "trade/i_order_broker.hpp"

#include <memory>
#include <string>

namespace mmtrade {
namespace opend {

std::string OrderBrokerModeFromEnv();
double GatewayHttpTimeoutSecFromEnv();
std::string GatewayUrlFromEnv();

// Returns NULL when ORDER_BROKER_MODE is unset/direct.
// gateway / gateway_fallback both return FallbackBroker (gateway + DirectBroker).
class TradeConnection;

std::unique_ptr<trade::IOrderBroker> CreateOrderBroker(const trade::DirectBrokerConfig &direct_cfg,
                                                        TradeConnection *shared_trade = NULL);

}  // namespace opend
}  // namespace mmtrade
