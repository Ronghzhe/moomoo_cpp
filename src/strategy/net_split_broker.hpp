#pragma once

#include "opend/account_types.hpp"

#include <string>
#include <utility>

namespace mmtrade {
namespace strategy {

std::pair<std::string, double> BrokerRowSideAndQty(const opend::PositionInfo &row);

}  // namespace strategy
}  // namespace mmtrade
