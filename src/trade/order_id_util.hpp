#pragma once

#include <cstdint>
#include <string>

namespace mmtrade {
namespace trade {

std::string FormatOrderId(std::uint64_t order_id);

// Prefer broker display id (orderIDEx) for logs/stats; fall back to internal orderID.
std::string ResolveDisplayOrderId(std::uint64_t order_id, const std::string &order_id_ex);

// Mirrors Python order_client.extract_order_id for string payloads.
std::string ExtractOrderIdString(const std::string &raw);

std::uint64_t ParseOrderIdUint64(const std::string &raw);

}  // namespace trade
}  // namespace mmtrade
