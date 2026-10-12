#pragma once

#include <string>

namespace mmtrade {
namespace config {

// Mirrors strategy_loader._normalize_execution_order_type
std::string NormalizeExecutionOrderType(const std::string &raw);

}  // namespace config
}  // namespace mmtrade
