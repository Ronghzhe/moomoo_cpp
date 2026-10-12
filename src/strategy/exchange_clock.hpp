#pragma once

#include "engine/session_close_util.hpp"

#include <string>

namespace mmtrade {
namespace strategy {

engine::LocalDateTime ExchangeLocalFromUnix(const std::string &code_full, double unix_ts);

}  // namespace strategy
}  // namespace mmtrade
