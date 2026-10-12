#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace mmtrade {
namespace task {

void ApplyEntryNotePreservation(nlohmann::json *payload, const nlohmann::json &old_raw);
void StampEntryNoteOnMapped(nlohmann::json *mapped, const nlohmann::json &raw_payload);
std::string NormalizeEntryTypeCode(const std::string &note);

}  // namespace task
}  // namespace mmtrade
