#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace mmtrade {
namespace task {

nlohmann::json BuildFTaskResult(const nlohmann::json *entry_result, const nlohmann::json *exit_result,
                                const nlohmann::json *error);
nlohmann::json ErrorForCode(const std::string &code, const std::string &hint);
nlohmann::json ComposeFTaskResultFromWorker(const nlohmann::json &run_result);

}  // namespace task
}  // namespace mmtrade
