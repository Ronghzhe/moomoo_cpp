#pragma once

#include <string>

namespace mmtrade {
namespace task {

std::string DefaultStrategyRoot();
std::string DefaultBackupDir();
std::string DefaultTaskBackupDir();
std::string DefaultTaskSystemDir();
std::string DefaultSystemLogPath();
std::string HkTemplatePath();
std::string UsTemplatePath();
std::string RunStrategyScriptPath();

std::string SafePathSegment(const std::string &name, const std::string &fallback = "_anonymous");

}  // namespace task
}  // namespace mmtrade
