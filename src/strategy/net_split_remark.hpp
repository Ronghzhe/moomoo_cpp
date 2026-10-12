#pragma once

#include <string>

namespace mmtrade {
namespace strategy {

std::string PlaceOrderRemarkUtf8(const std::string &remark, int max_bytes = 64);

}  // namespace strategy
}  // namespace mmtrade
