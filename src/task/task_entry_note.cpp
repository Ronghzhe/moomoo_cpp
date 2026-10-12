#include "task/task_entry_note.hpp"

#include <algorithm>
#include <cctype>

namespace mmtrade {
namespace task {

namespace {

bool IsSystemTaskNote(const std::string &note) {
  std::string s = note;
  for (size_t i = 0; i < s.size(); ++i) {
    s[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
  }
  if (s.empty()) {
    return false;
  }
  if (s.compare(0, 8, "account=") == 0) {
    return true;
  }
  return s.find("web_position_seed") != std::string::npos || s.find("web_manual_close") != std::string::npos;
}

std::string LookupEntryType(const std::string &s) {
  static const char *kAliases[][2] = {
      {"跳空", "gap"}, {"gap", "gap"}, {"连续k", "consecutive"}, {"连续K", "consecutive"},
      {"consecutive", "consecutive"}, {"回踩", "pullback"}, {"pullback", "pullback"},
  };
  for (size_t i = 0; i < sizeof(kAliases) / sizeof(kAliases[0]); ++i) {
    if (s == kAliases[i][0]) {
      return kAliases[i][1];
    }
    std::string lower = s;
    for (size_t j = 0; j < lower.size(); ++j) {
      lower[j] = static_cast<char>(std::tolower(static_cast<unsigned char>(lower[j])));
    }
    std::string alias = kAliases[i][0];
    for (size_t j = 0; j < alias.size(); ++j) {
      alias[j] = static_cast<char>(std::tolower(static_cast<unsigned char>(alias[j])));
    }
    if (lower == alias) {
      return kAliases[i][1];
    }
  }
  return "";
}

bool IsEntryTaskType(const std::string &tt) {
  return tt == "entry_now" || tt == "monitor_entry" || tt == "monitor_all";
}

std::string MappableEntryNoteFromParams(const nlohmann::json &params) {
  const std::string en = params.value("entry_note", "");
  if (!en.empty() && !NormalizeEntryTypeCode(en).empty()) {
    return en;
  }
  const std::string et = params.value("entry_type", "");
  if (et == "gap" || et == "consecutive" || et == "pullback") {
    return en.empty() ? et : en;
  }
  if (IsEntryTaskType(params.value("task_type", ""))) {
    const std::string note = params.value("note", "");
    if (!note.empty() && !NormalizeEntryTypeCode(note).empty()) {
      return note;
    }
  }
  return "";
}

}  // namespace

std::string NormalizeEntryTypeCode(const std::string &note) {
  const std::string s = note;
  if (s.empty() || IsSystemTaskNote(s)) {
    return "";
  }
  return LookupEntryType(s);
}

void ApplyEntryNotePreservation(nlohmann::json *payload, const nlohmann::json &old_raw) {
  if (payload == NULL) {
    return;
  }
  const std::string preserved = old_raw.is_object() ? MappableEntryNoteFromParams(old_raw) : "";
  const std::string tt = payload->value("task_type", "");
  const std::string incoming = IsEntryTaskType(tt) ? MappableEntryNoteFromParams(*payload) : "";
  const std::string note_text = !incoming.empty() ? incoming : preserved;
  if (note_text.empty()) {
    return;
  }
  (*payload)["entry_note"] = note_text;
  const std::string code = NormalizeEntryTypeCode(note_text);
  if (!code.empty()) {
    (*payload)["entry_type"] = code;
  }
}

void StampEntryNoteOnMapped(nlohmann::json *mapped, const nlohmann::json &raw_payload) {
  if (mapped == NULL) {
    return;
  }
  const std::string note_text = raw_payload.value("entry_note", "");
  const std::string code = raw_payload.value("entry_type", "");
  if (note_text.empty() && code != "gap" && code != "consecutive" && code != "pullback") {
    return;
  }
  if (!mapped->contains("symbols") || !(*mapped)["symbols"].is_array() || (*mapped)["symbols"].empty()) {
    return;
  }
  if (!note_text.empty()) {
    (*mapped)["symbols"][0]["entry_note"] = note_text;
  }
  if (code == "gap" || code == "consecutive" || code == "pullback") {
    (*mapped)["symbols"][0]["entry_type"] = code;
  }
}

}  // namespace task
}  // namespace mmtrade
