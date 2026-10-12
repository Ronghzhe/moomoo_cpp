#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace mmtrade {
namespace task {

struct MappedConfigResult {
  nlohmann::json config;
  nlohmann::json summary;
};

MappedConfigResult BuildStrategyConfig(const std::string &task_name, const nlohmann::json &params,
                                       bool paper_mode);

}  // namespace task
}  // namespace mmtrade
