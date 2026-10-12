#include "task/task_backup_store.hpp"

#include "task/task_entry_note.hpp"
#include "task/task_paths.hpp"

#include <fstream>
#include <cerrno>
#include <sys/stat.h>

namespace mmtrade {
namespace task {

namespace {

const char *kRuntimePositionKeys[] = {"opened_qty", "opened_price", "closed_qty", "closed_price"};
const char *kAlwaysRefreshKeys[] = {"sim_market_pct", "order_type"};

bool EnsureDir(const std::string &path) {
  return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

nlohmann::json DeepOverlayDict(const nlohmann::json &base, const nlohmann::json &fresh) {
  nlohmann::json out = base;
  for (nlohmann::json::const_iterator it = fresh.begin(); it != fresh.end(); ++it) {
    const std::string &k = it.key();
    if (out.contains(k) && out[k].is_object() && it.value().is_object()) {
      out[k] = DeepOverlayDict(out[k], it.value());
    } else {
      out[k] = it.value();
    }
  }
  return out;
}

nlohmann::json OverlaySymbol(const nlohmann::json &base0, const nlohmann::json &fresh0,
                             bool preserve_status) {
  nlohmann::json merged0 = base0.is_object() ? base0 : nlohmann::json::object();
  for (nlohmann::json::const_iterator it = fresh0.begin(); it != fresh0.end(); ++it) {
    const std::string &sk = it.key();
    bool skip = false;
    for (size_t i = 0; i < sizeof(kAlwaysRefreshKeys) / sizeof(kAlwaysRefreshKeys[0]); ++i) {
      if (sk == kAlwaysRefreshKeys[i]) {
        merged0[sk] = it.value();
        skip = true;
        break;
      }
    }
    if (skip) {
      continue;
    }
    for (size_t i = 0; i < sizeof(kRuntimePositionKeys) / sizeof(kRuntimePositionKeys[0]); ++i) {
      if (sk == kRuntimePositionKeys[i] && merged0.contains(sk)) {
        skip = true;
        break;
      }
    }
    if (skip) {
      continue;
    }
    if (preserve_status && sk == "status" && merged0.contains("status")) {
      continue;
    }
    merged0[sk] = it.value();
  }
  merged0["startup_notify_once"] = true;
  return merged0;
}

nlohmann::json ReadJsonFile(const std::string &path) {
  std::ifstream ifs(path.c_str());
  if (!ifs) {
    return nlohmann::json();
  }
  nlohmann::json j;
  ifs >> j;
  return j;
}

void WriteJsonFile(const std::string &path, const nlohmann::json &j) {
  std::ofstream ofs(path.c_str());
  ofs << j.dump(2);
}

void ApplyRawSimMarketPct(nlohmann::json *merged, const nlohmann::json &raw_payload) {
  if (merged == NULL || !raw_payload.contains("sim_market_pct") || raw_payload.at("sim_market_pct").is_null()) {
    return;
  }
  if (!merged->contains("symbols") || !(*merged)["symbols"].is_array() || (*merged)["symbols"].empty()) {
    return;
  }
  (*merged)["symbols"][0]["sim_market_pct"] = raw_payload.at("sim_market_pct");
}

}  // namespace

std::pair<std::string, std::string> TaskBackupPaths(const std::string &backup_dir, const std::string &f_user,
                                                    const std::string &f_task_name) {
  const std::string su = SafePathSegment(f_user);
  const std::string st = SafePathSegment(f_task_name, "_noname");
  const std::string dir = backup_dir + "/" + su + "/" + st;
  return std::make_pair(dir + "/" + st + "_raw.json", dir + "/" + st + "_mapped.json");
}

nlohmann::json MergeMappedIntoExisting(const nlohmann::json *existing, const nlohmann::json &fresh,
                                       bool preserve_symbol_status) {
  if (existing == NULL || existing->is_null() || !existing->is_object()) {
    nlohmann::json out = fresh;
    if (out.contains("symbols") && out["symbols"].is_array() && !out["symbols"].empty()) {
      out["symbols"][0]["startup_notify_once"] = true;
    }
    return out;
  }

  nlohmann::json out = *existing;
  for (nlohmann::json::const_iterator it = fresh.begin(); it != fresh.end(); ++it) {
    const std::string &k = it.key();
    if (k == "symbols" && it.value().is_array() && !it.value().empty() && it.value()[0].is_object()) {
      nlohmann::json old0 = nlohmann::json::object();
      if (out.contains("symbols") && out["symbols"].is_array() && !out["symbols"].empty()) {
        old0 = out["symbols"][0];
      }
      nlohmann::json sym = OverlaySymbol(old0, it.value()[0], preserve_symbol_status);
      if (!out.contains("symbols") || !out["symbols"].is_array() || out["symbols"].empty()) {
        out["symbols"] = nlohmann::json::array({sym});
      } else {
        out["symbols"][0] = sym;
      }
    } else if (it.value().is_object() && out.contains(k) && out[k].is_object()) {
      out[k] = DeepOverlayDict(out[k], it.value());
    } else {
      out[k] = it.value();
    }
  }
  if (out.contains("symbols") && out["symbols"].is_array() && !out["symbols"].empty()) {
    out["symbols"][0]["startup_notify_once"] = true;
  }
  return out;
}

BackupWriteResult WriteTaskPairBackups(const std::string &backup_dir, const std::string &f_user,
                                       const std::string &f_task_name, nlohmann::json raw_payload,
                                       const nlohmann::json &fresh_mapped) {
  std::pair<std::string, std::string> paths = TaskBackupPaths(backup_dir, f_user, f_task_name);
  const std::string &raw_p = paths.first;
  const std::string &mapped_p = paths.second;
  const size_t slash = raw_p.find_last_of('/');
  if (slash != std::string::npos) {
    EnsureDir(raw_p.substr(0, slash));
  }

  nlohmann::json old_raw = ReadJsonFile(raw_p);
  if (old_raw.is_object()) {
    ApplyEntryNotePreservation(&raw_payload, old_raw);
  }

  nlohmann::json old_mapped = ReadJsonFile(mapped_p);
  const std::string tt = raw_payload.value("task_type", "");
  const bool preserve_st = (tt == "monitor_all");
  const nlohmann::json *old_ptr = old_mapped.is_object() ? &old_mapped : NULL;
  nlohmann::json merged = MergeMappedIntoExisting(old_ptr, fresh_mapped, preserve_st);
  ApplyRawSimMarketPct(&merged, raw_payload);
  StampEntryNoteOnMapped(&merged, raw_payload);

  WriteJsonFile(raw_p, raw_payload);
  WriteJsonFile(mapped_p, merged);

  BackupWriteResult out;
  out.raw_path = raw_p;
  out.mapped_path = mapped_p;
  out.merged_mapped = merged;
  return out;
}

}  // namespace task
}  // namespace mmtrade
