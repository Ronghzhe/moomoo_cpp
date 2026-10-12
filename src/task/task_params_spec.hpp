#pragma once

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace mmtrade {
namespace task {

void ValidateTaskParams(const nlohmann::json &payload);

}  // namespace task
}  // namespace mmtrade
