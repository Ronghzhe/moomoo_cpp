#include "config/config_persist.hpp"

#include "log/ledger_ts.hpp"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace mmtrade {
namespace config {

namespace {

// ordered_json preserves key order from the source file (Python json.dump parity).
// nlohmann::json uses std::map and re-sorts keys alphabetically on dump().
using json = nlohmann::ordered_json;

std::string TrimCopy(const std::string &s) {
  size_t start = 0;
  while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) {
    ++start;
  }
  size_t end = s.size();
  while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t')) {
    --end;
  }
  return s.substr(start, end - start);
}

bool IsValidInitialQty(double v) {
  return std::isfinite(v) && v > 0.0 && v < 1e15;
}

void WriteConfigAtomic(const std::string &config_path, const json &doc) {
  const std::string tmp = config_path + ".tmp";
  {
    std::ofstream ofs(tmp.c_str());
    if (!ofs.good()) {
      throw std::runtime_error("persist: cannot write config tmp");
    }
    ofs << doc.dump(2) << "\n";
  }
  if (std::rename(tmp.c_str(), config_path.c_str()) != 0) {
    throw std::runtime_error("persist: config rename failed");
  }
}

void PatchSymbolRuntimeJson(json *item, const SymbolConfig &sym, bool startup_only) {
  if (item == NULL || !item->is_object()) {
    return;
  }
  if (startup_only) {
    (*item)["startup_notify_once"] = false;
  } else {
    (*item)["status"] = sym.status;
    (*item)["opened_qty"] = sym.opened_qty;
    (*item)["opened_price"] = sym.opened_price;
    (*item)["closed_qty"] = sym.closed_qty;
    (*item)["closed_price"] = sym.closed_price;
    if (IsValidInitialQty(sym.initial_qty)) {
      (*item)["initial_qty"] = sym.initial_qty;
    }
  }
  (*item)["runtime_updated_at"] = mmtrade::log::RuntimeUpdatedAtUtc();
}

bool UpdateSymbolByCode(json *raw, const std::string &code, const SymbolConfig &sym,
                       bool startup_only) {
  if (raw == NULL || !raw->is_object()) {
    return false;
  }
  const std::string want = TrimCopy(code);
  if (raw->contains("symbols") && (*raw)["symbols"].is_array()) {
    json &arr = (*raw)["symbols"];
    for (size_t i = 0; i < arr.size(); ++i) {
      if (!arr[i].is_object()) {
        continue;
      }
      const std::string got = TrimCopy(arr[i].value("code", std::string()));
      if (got != want) {
        continue;
      }
      PatchSymbolRuntimeJson(&arr[i], sym, startup_only);
      return true;
    }
    return false;
  }
  if (raw->contains("symbol") && (*raw)["symbol"].is_object()) {
    json &symo = (*raw)["symbol"];
    const std::string got = TrimCopy(symo.value("code", std::string()));
    if (got != want) {
      return false;
    }
    PatchSymbolRuntimeJson(&symo, sym, startup_only);
    return true;
  }
  return false;
}

void PersistSymbolRuntimeMain(const std::string &config_path, const SymbolConfig &sym,
                              bool startup_only) {
  std::ifstream ifs(config_path.c_str());
  if (!ifs.good()) {
    throw std::runtime_error("persist: cannot open config");
  }
  json raw;
  ifs >> raw;
  if (!UpdateSymbolByCode(&raw, sym.code, sym, startup_only)) {
    throw std::runtime_error("persist: JSON 中未找到 code=" + sym.code);
  }
  WriteConfigAtomic(config_path, raw);
}

}  // namespace

std::string ResolveCsvDir(const std::string &config_path, const std::string &csv_dir_raw) {
  (void)config_path;
  std::string raw = TrimCopy(csv_dir_raw);
  if (raw.empty()) {
    raw = "./daily_logs";
  }
  if (!raw.empty() && raw[0] == '/') {
    return raw;
  }
  const char *env_root = std::getenv("MOOMOO_CPP_ROOT");
  std::string root;
  if (env_root != NULL && env_root[0] != '\0') {
    root = env_root;
  } else {
    root = ".";
  }
  if (root[root.size() - 1] != '/') {
    root.push_back('/');
  }
  return root + raw;
}

void PersistStrategyConfigRuntime(const std::string &config_path, const SymbolConfig &sym) {
  PersistSymbolRuntimeMain(config_path, sym, false);
}

void PersistStartupNotifyOnceConsumed(const std::string &config_path, const std::string &code) {
  SymbolConfig dummy;
  dummy.code = code;
  PersistSymbolRuntimeMain(config_path, dummy, true);
}

}  // namespace config
}  // namespace mmtrade
