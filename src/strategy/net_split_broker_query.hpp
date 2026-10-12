#pragma once

#include "opend/order_client.hpp"

#include <string>
#include <utility>

namespace mmtrade {
namespace strategy {

std::pair<double, double> BrokerLongShortQty(opend::OrderClient *client,
                                             const std::string &code);

}  // namespace strategy
}  // namespace mmtrade
