#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace mmtrade {
namespace task {

std::pair<std::string, std::string> TaskBackupPaths(const std::string &backup_dir, const std::string &f_user,
                                                    const std::string &f_task_name);

nlohmann::json MergeMappedIntoExisting(const nlohmann::json *existing, const nlohmann::json &fresh,
                                       bool preserve_symbol_status);

struct BackupWriteResult {
  std::string raw_path;
  std::string mapped_path;
  nlohmann::json merged_mapped;
};

BackupWriteResult WriteTaskPairBackups(const std::string &backup_dir, const std::string &f_user,
                                       const std::string &f_task_name, nlohmann::json raw_payload,
                                       const nlohmann::json &fresh_mapped);

}  // namespace task
}  // namespace mmtrade
