#include "task/trade_executor.hpp"

// 单任务执行实现：等 backup → run_strategy.sh → 解析 exit_code / run_outcome.json。

#include "task/task_backup_store.hpp"
#include "task/task_latency_recorder.hpp"
#include "task/task_paths.hpp"
#include "task/task_result.hpp"
#include "trade/order_errors.hpp"

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cmath>
#include <ctime>
#include <mutex>
#include <string>

namespace mmtrade {
namespace task {

const double kUnlockRateLimitCooldownSec = 35.0;

std::string ToUpperCopyLocal(std::string s) {
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] >= 'a' && s[i] <= 'z') {
      s[i] = static_cast<char>(s[i] - 'a' + 'A');
    }
  }
  return s;
}

nlohmann::json ParseTaskParamsJson(const std::string &raw) {
  try {
    const nlohmann::json j = nlohmann::json::parse(raw.empty() ? "{}" : raw);
    return j.is_object() ? j : nlohmann::json::object();
  } catch (...) {
    return nlohmann::json::object();
  }
}

namespace {

bool EnsureDirRecursive(const std::string &path, mode_t mode) {
  if (path.empty()) {
    return false;
  }
  std::string built;
  built.reserve(path.size());
  for (size_t i = 0; i < path.size(); ++i) {
    const char c = path[i];
    built.push_back(c);
    if (c != '/') {
      continue;
    }
    if (built.size() <= 1) {
      continue;
    }
    if (mkdir(built.c_str(), mode) != 0 && errno != EEXIST) {
      return false;
    }
  }
  if (mkdir(path.c_str(), mode) != 0 && errno != EEXIST) {
    return false;
  }
  return true;
}

std::string NowIso() {
  const std::time_t now = std::time(NULL);
  std::tm tm_buf;
  localtime_r(&now, &tm_buf);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);
  return std::string(buf);
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

std::string SanitizeUtf8(const std::string &in) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size();) {
    const unsigned char c = static_cast<unsigned char>(in[i]);
    if (c <= 0x7F) {
      out.push_back(static_cast<char>(c));
      ++i;
      continue;
    }
    int len = 0;
    if ((c & 0xE0) == 0xC0) {
      len = 2;
    } else if ((c & 0xF0) == 0xE0) {
      len = 3;
    } else if ((c & 0xF8) == 0xF0) {
      len = 4;
    } else {
      out.push_back('?');
      ++i;
      continue;
    }
    if (i + static_cast<size_t>(len) > in.size()) {
      out.push_back('?');
      ++i;
      continue;
    }
    bool valid = true;
    for (int j = 1; j < len; ++j) {
      if ((static_cast<unsigned char>(in[i + static_cast<size_t>(j)]) & 0xC0) != 0x80) {
        valid = false;
        break;
      }
    }
    if (valid) {
      out.append(in, i, static_cast<size_t>(len));
      i += static_cast<size_t>(len);
    } else {
      out.push_back('?');
      ++i;
    }
  }
  return out;
}

std::string TailText(const std::string &path, size_t max_chars) {
  std::ifstream ifs(path.c_str());
  if (!ifs) {
    return "";
  }
  std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  if (text.size() > max_chars) {
    text = text.substr(text.size() - max_chars);
  }
  return SanitizeUtf8(text);
}

std::string SafeJsonDump(const nlohmann::json &j) {
  try {
    return j.dump();
  } catch (const std::exception &) {
    nlohmann::json copy = j;
    if (copy.contains("error") && copy["error"].is_string()) {
      copy["error"] = SanitizeUtf8(copy["error"].get<std::string>());
    }
    try {
      return copy.dump();
    } catch (const std::exception &) {
      return "{\"error\":\"json_dump_failed\"}";
    }
  }
}

double CoerceTradeFloat(const nlohmann::json &v) {
  if (v.is_null() || !v.is_number()) {
    return 0.0;
  }
  return v.get<double>();
}

nlohmann::json FTaskResultFromMappedDisk(const std::string &mapped_path) {
  const nlohmann::json data = ReadJsonFile(mapped_path);
  if (!data.contains("symbols") || !data["symbols"].is_array() || data["symbols"].empty()) {
    return BuildFTaskResult(NULL, NULL, NULL);
  }
  const nlohmann::json &s0 = data["symbols"][0];
  const double e_qty = CoerceTradeFloat(s0.value("opened_qty", 0.0));
  const double e_px = CoerceTradeFloat(s0.value("opened_price", 0.0));
  const double x_qty = CoerceTradeFloat(s0.value("closed_qty", 0.0));
  const double x_px = CoerceTradeFloat(s0.value("closed_price", 0.0));
  const double tol = 1e-9;
  nlohmann::json entry_er = nlohmann::json::object();
  if (e_qty > tol) {
    entry_er["qty"] = e_qty;
    entry_er["avg_price"] = (e_px > tol) ? nlohmann::json(e_px) : nlohmann::json();
  }
  nlohmann::json exit_er = nlohmann::json::object();
  if (x_qty > tol) {
    exit_er["qty"] = x_qty;
    exit_er["avg_price"] = (x_px > tol) ? nlohmann::json(x_px) : nlohmann::json();
  }
  return BuildFTaskResult(entry_er.empty() ? NULL : &entry_er, exit_er.empty() ? NULL : &exit_er, NULL);
}

// 磁盘上 mapped.json 显示全部 enabled symbol 已托管完毕时，跳过 shell 启动。
bool TaskCompletedFromMappedDisk(const std::string &mapped_path) {
  const nlohmann::json data = ReadJsonFile(mapped_path);
  if (!data.contains("symbols") || !data["symbols"].is_array() || data["symbols"].empty()) {
    return false;
  }
  for (size_t i = 0; i < data["symbols"].size(); ++i) {
    const nlohmann::json &s = data["symbols"][i];
    if (!s.value("enabled", true)) {
      continue;
    }
    const int st = s.value("status", 0);
    if (st == kStatusDone) {
      continue;
    }
    const double opened_qty = CoerceTradeFloat(s.value("opened_qty", 0.0));
    const double closed_qty = CoerceTradeFloat(s.value("closed_qty", 0.0));
    const double open_left = opened_qty - closed_qty;
    if (closed_qty > 1e-9 && open_left <= 1e-9) {
      continue;
    }
    const bool exit_enabled = s.value("exit_enabled", true);
    if (!exit_enabled && st == kStatusReady && opened_qty > 1e-9) {
      continue;
    }
    return false;
  }
  return true;
}

// 将 cmd 输出追加到 log_path；可选 timeout 秒数（124=超时）。
int RunShellCommand(const std::string &cmd, const std::string &log_path, int timeout_sec) {
  const std::string full = cmd + " >> " + log_path + " 2>&1";
  if (timeout_sec > 0) {
    const std::string wrapped = "timeout " + std::to_string(timeout_sec) + " " + full;
    return std::system(wrapped.c_str());
  }
  return std::system(full.c_str());
}

// Mirrors Python trade_executor._in_rth_open_burst_window:
// America/New_York and Asia/Shanghai, Mon–Fri, local 09:25–09:40 inclusive.
bool InRthOpenBurstWindow(std::time_t now_ts) {
  static std::mutex tz_mu;
  std::lock_guard<std::mutex> lock(tz_mu);

  const char *old_tz = std::getenv("TZ");
  const bool had_tz = (old_tz != NULL);
  const std::string old_tz_copy = had_tz ? std::string(old_tz) : std::string();

  static const char *kZones[] = {"America/New_York", "Asia/Shanghai"};
  bool in_window = false;
  for (std::size_t i = 0; i < sizeof(kZones) / sizeof(kZones[0]); ++i) {
    ::setenv("TZ", kZones[i], 1);
    ::tzset();
    struct tm local_tm;
    if (::localtime_r(&now_ts, &local_tm) == NULL) {
      continue;
    }
    // tm_wday: 0=Sun ... 6=Sat; Python weekday() Mon=0..Sun=6, skip >=5 (Sat/Sun).
    if (local_tm.tm_wday == 0 || local_tm.tm_wday == 6) {
      continue;
    }
    const int mins = local_tm.tm_hour * 60 + local_tm.tm_min;
    if (mins >= (9 * 60 + 25) && mins <= (9 * 60 + 40)) {
      in_window = true;
      break;
    }
  }

  if (had_tz) {
    ::setenv("TZ", old_tz_copy.c_str(), 1);
  } else {
    ::unsetenv("TZ");
  }
  ::tzset();
  return in_window;
}

std::string JsonStringOrEmpty(const nlohmann::json &j, const char *key) {
  if (j.contains(key) && j[key].is_string()) {
    return j[key].get<std::string>();
  }
  return "";
}

// Mapped 任务把标的放在 symbols[0].code（如 US.AAPL），顶层往往没有 symbol。
// 品种目录 latency.csv 依赖 staging 里的 symbol。
std::string LatencySymbolFromMapped(const nlohmann::json &mapped) {
  const std::string top = JsonStringOrEmpty(mapped, "symbol");
  if (!top.empty()) {
    return top;
  }
  if (mapped.contains("symbols") && mapped["symbols"].is_array()) {
    for (size_t i = 0; i < mapped["symbols"].size(); ++i) {
      const nlohmann::json &sym = mapped["symbols"][i];
      if (!sym.is_object()) {
        continue;
      }
      if (sym.contains("enabled") && sym["enabled"].is_boolean() && !sym["enabled"].get<bool>()) {
        continue;
      }
      std::string code = JsonStringOrEmpty(sym, "code");
      if (code.empty()) {
        code = JsonStringOrEmpty(sym, "symbol");
      }
      if (!code.empty()) {
        return code;
      }
    }
  }
  const std::string stock_code = JsonStringOrEmpty(mapped, "stock_code");
  if (stock_code.find('.') != std::string::npos) {
    return stock_code;
  }
  std::string stock_type = JsonStringOrEmpty(mapped, "stock_type");
  if (stock_type.empty() && mapped.contains("account") && mapped["account"].is_object()) {
    stock_type = JsonStringOrEmpty(mapped["account"], "trd_market");
  }
  if (!stock_type.empty() && !stock_code.empty()) {
    return ToUpperCopyLocal(stock_type) + "." + stock_code;
  }
  return "";
}

// 仅开盘窗口内：多任务同时 ready 时按 f_id 取模 sleep，减轻 OpenD 同时连/订阅压力。
void MaybeOpenBurstStagger(const TaskRow &task, SystemLogger *log, double slot_sec, int buckets) {
  if (slot_sec <= 0 || buckets <= 0) {
    return;
  }
  if (!InRthOpenBurstWindow(std::time(NULL))) {
    return;
  }
  const double delay = static_cast<double>(task.f_id % buckets) * slot_sec;
  if (delay <= 0) {
    return;
  }
  if (log != NULL) {
    log->Info("executor_open_stagger f_id=" + std::to_string(static_cast<long long>(task.f_id)) +
              " task=" + task.f_task_name + " sleep=" + std::to_string(delay) + "s");
  }
  usleep(static_cast<useconds_t>(delay * 1000000.0));
}

}  // namespace

std::string InferTaskMarket(const TaskRow &task) {
  const nlohmann::json params = ParseTaskParamsJson(task.f_task_params);
  const std::string strategy_name = params.value("strategy_name", "");
  const std::string symbol = ToUpperCopyLocal(params.value("symbol", ""));
  if (strategy_name.find("-HK") != std::string::npos || symbol.find("HK.") == 0) {
    return "HK";
  }
  if (strategy_name.find("-US") != std::string::npos || symbol.find("US.") == 0) {
    return "US";
  }
  const std::string stock_type = ToUpperCopyLocal(params.value("stock_type", ""));
  if (stock_type == "HK") {
    return "HK";
  }
  if (stock_type == "US") {
    return "US";
  }
  return "US";
}

std::string InferTaskSymbol(const TaskRow &task) {
  const nlohmann::json params = ParseTaskParamsJson(task.f_task_params);
  return ToUpperCopyLocal(params.value("symbol", ""));
}

bool EntryNetTestEnabled() {
  const char *raw = std::getenv("EXECUTOR_ENTRY_NET_TEST");
  if (raw == NULL || raw[0] == '\0') {
    return false;
  }
  const std::string v = ToUpperCopyLocal(raw);
  return v == "1" || v == "TRUE" || v == "YES" || v == "ON";
}

// Executor 单任务主流程；优先 strategy pool，失败则 fallback shell。
ExecutorRunResult RunOneTask(const TaskRow &task, int timeout_sec, SystemLogger *log,
                             double open_stagger_slot_sec, int open_stagger_buckets,
                             StrategyWorkerPool *pool) {
  ExecutorRunResult out;
  out.ok = false;
  const std::string staging_path = TaskLatencyRecorder::StagingPath(task.f_id, task.f_task_name);
  TaskLatencyRecorder lat;
  lat.Configure(staging_path, TaskLatencyRecorder::DefaultCsvPath());
  const std::string task_name = task.f_task_name;
  const std::string f_user = task.f_user;

  if (lat.Enabled()) {
    lat.RecordTimestampMs("executor_claim_ms");
    lat.RecordValue("f_id", task.f_id);
    lat.RecordValue("f_task_type", task.f_task_type);
    lat.RecordString("task_name", task_name);
    lat.RecordString("f_user", f_user);
  }

  MaybeOpenBurstStagger(task, log, open_stagger_slot_sec, open_stagger_buckets);
  const std::string started = NowIso();
  std::pair<std::string, std::string> paths = TaskBackupPaths(DefaultTaskBackupDir(), f_user, task_name);
  const std::string &raw_path = paths.first;
  const std::string &mapped_path = paths.second;

  const double wait_until = static_cast<double>(std::time(NULL)) + 45.0;
  while (!std::ifstream(mapped_path.c_str()) && static_cast<double>(std::time(NULL)) < wait_until) {
    usleep(400000);
  }
  if (lat.Enabled()) {
    lat.RecordTimestampMs("mapped_ready_ms");
  }
  if (!std::ifstream(mapped_path.c_str())) {
    out.payload = nlohmann::json{{"task_name", task_name},
                                   {"start_time", started},
                                   {"finish_time", NowIso()},
                                   {"ok", false},
                                   {"exit_code", 1},
                                   {"error", "mapped json not found after wait: " + mapped_path},
                                   {"note", "executor cannot find mapped file"},
                                   {"f_user", f_user},
                                   {"raw_backup", raw_path},
                                   {"mapped_backup", mapped_path}};
    return out;
  }

  const nlohmann::json mapped_cfg = ReadJsonFile(mapped_path);
  const std::string mode = mapped_cfg.value("mode", "paper") == "real" ? "real" : "paper";
  if (lat.Enabled()) {
    const std::string latency_symbol = LatencySymbolFromMapped(mapped_cfg);
    if (!latency_symbol.empty()) {
      lat.RecordString("symbol", latency_symbol);
    }
    if (mapped_cfg.contains("stock_code") && mapped_cfg["stock_code"].is_string()) {
      lat.RecordString("stock_code", mapped_cfg["stock_code"].get<std::string>());
    }
    if (mapped_cfg.contains("trade_qty") && mapped_cfg["trade_qty"].is_number()) {
      lat.RecordValue("trade_qty", mapped_cfg["trade_qty"].get<int64_t>());
    }
  }

  if (TaskCompletedFromMappedDisk(mapped_path)) {
    if (log != NULL) {
      log->Info("executor_skip_already_done task=" + task_name + " mapped=" + mapped_path);
    }
    out.payload = nlohmann::json{{"task_name", task_name},
                                 {"start_time", started},
                                 {"finish_time", NowIso()},
                                 {"duration_sec", 0.0},
                                 {"ok", true},
                                 {"exit_code", 0},
                                 {"error", ""},
                                 {"note", "executor skipped: position already closed on disk"},
                                 {"mode", mode},
                                 {"f_user", f_user},
                                 {"raw_backup", raw_path},
                                 {"mapped_backup", mapped_path},
                                 {"f_task_result", FTaskResultFromMappedDisk(mapped_path)},
                                 {"task_completed", true}};
    out.ok = true;
    if (lat.Enabled()) {
      lat.RecordTimestampMs("shell_end_ms");
      (void)lat.FinalizeCsvRow(kStatusDone, 0);
    }
    return out;
  }

  const std::string latency_staging = staging_path;
  const std::string latency_csv = TaskLatencyRecorder::DefaultCsvPath();
  const std::string run_log_root = DefaultTaskSystemDir() + "/run_logs";
  const std::string run_log_dir = run_log_root + "/" + SafePathSegment(f_user);
  if (!EnsureDirRecursive(run_log_dir, 0755)) {
    out.payload = nlohmann::json{{"task_name", task_name},
                                 {"start_time", started},
                                 {"finish_time", NowIso()},
                                 {"ok", false},
                                 {"exit_code", 1},
                                 {"error", "failed to create run_log dir: " + run_log_dir},
                                 {"note", "executor setup failed"},
                                 {"f_user", f_user},
                                 {"raw_backup", raw_path},
                                 {"mapped_backup", mapped_path}};
    return out;
  }
  std::string run_log_path;
  const char *run_log_enable = std::getenv("MOOMOO_STRATEGY_RUN_LOG");
  const bool want_run_log =
      run_log_enable != NULL && run_log_enable[0] != '\0' && run_log_enable[0] != '0';
  if (want_run_log) {
    run_log_path = run_log_dir + "/" + SafePathSegment(task_name, "_noname") + ".log";
    std::ofstream lf(run_log_path.c_str(), std::ios::app);
    lf << "\n=== " << started << " START task=" << task_name << " mode=" << mode << " ===\n";
  }

  const double t0 = static_cast<double>(std::time(NULL));
  if (lat.Enabled()) {
    lat.RecordTimestampMs("shell_start_ms");
  }

  int code = 1;
  bool ok = false;
  nlohmann::json run_outcome;
  std::string note;
  std::string via = "shell";

  auto pool_transport_fail = [](const std::string &err) -> bool {
    if (err.empty()) {
      return false;
    }
    return err.find("unavailable") != std::string::npos ||
           err.find("timeout") != std::string::npos ||
           err.find("eof") != std::string::npos ||
           err.find("write") != std::string::npos ||
           err.find("bad worker") != std::string::npos ||
           err.find("no idle") != std::string::npos;
  };

  if (pool != NULL && pool->enabled()) {
    StrategyPoolRunRequest preq;
    preq.config_path = mapped_path;
    preq.latency_staging = latency_staging;
    preq.latency_csv = latency_csv;
    preq.run_log = run_log_path;
    preq.timeout_sec = timeout_sec;
    if (log != NULL) {
      log->Info("executor_run_pool task=" + task_name + " mode=" + mode + " mapped=" + mapped_path +
                (run_log_path.empty() ? " run_log=off" : " run_log=" + run_log_path));
    }
    const StrategyPoolRunResult pr = pool->Run(preq, log);
    if (pr.used_pool && !pool_transport_fail(pr.error)) {
      via = "pool";
      code = pr.exit_code;
      ok = pr.ok;
      run_outcome = pr.run_outcome;
      note = ok ? "executor finished (pool)" : (code == 2 ? "trading_fatal" : "executor failed");
      if (!pr.error.empty() && !ok) {
        note = note + " " + pr.error;
      }
    } else if (pr.used_pool && pool_transport_fail(pr.error)) {
      if (log != NULL) {
        log->Info("executor_pool_fallback task=" + task_name + " err=" + pr.error);
      }
      via = "shell_fallback";
    }
  }

  if (via == "shell" || via == "shell_fallback") {
    const std::string run_sh = RunStrategyScriptPath();
    const std::string cmd =
        "MOOMOO_TASK_LATENCY_STAGING=" + latency_staging + " MOOMOO_TASK_LATENCY_CSV=" + latency_csv +
        " SKIP_BUILD=1 " + run_sh + " -c " + mapped_path;
    if (log != NULL) {
      log->Info("executor_run task=" + task_name + " mode=" + mode + " cmd=" + cmd +
                " run_log=" + run_log_path +
                (via == "shell_fallback" ? " via=shell_fallback" : ""));
    }
    const std::string shell_log = run_log_path.empty() ? "/dev/null" : run_log_path;
    const int code_raw = RunShellCommand(cmd, shell_log, timeout_sec);
    code = code_raw;
    if (code_raw == 124 * 256 || code_raw == 124) {
      code = 124;
    } else if (code_raw != 0) {
      code = WIFEXITED(code_raw) ? WEXITSTATUS(code_raw) : 1;
    }
    ok = (code == 0);
    run_outcome = ReadJsonFile(mapped_path.substr(0, mapped_path.find_last_of('/')) +
                               "/run_outcome.json");
    if (code == 2) {
      ok = false;
    } else if (code != 0 && run_outcome.is_object() && run_outcome.value("fatal", false)) {
      ok = false;
    }
    note = ok ? "executor finished" : "trading_fatal";
    if (!ok && code != 2) {
      note = "executor failed";
    }
  }

  {
    std::ofstream lf(run_log_path.c_str(), std::ios::app);
    lf << "\n=== " << NowIso() << " END task=" << task_name << " exit_code=" << code
       << " via=" << via << " ===\n";
  }
  if (lat.Enabled()) {
    lat.RecordTimestampMs("shell_end_ms");
  }

  if (via == "pool") {
    if (!run_outcome.is_object()) {
      run_outcome = ReadJsonFile(mapped_path.substr(0, mapped_path.find_last_of('/')) +
                                 "/run_outcome.json");
    }
    if (code == 2) {
      ok = false;
    } else if (code != 0 && run_outcome.is_object() && run_outcome.value("fatal", false)) {
      ok = false;
    }
  }

  out.payload = nlohmann::json{{"task_name", task_name},
                               {"start_time", started},
                               {"finish_time", NowIso()},
                               {"duration_sec", static_cast<double>(std::time(NULL)) - t0},
                               {"ok", ok},
                               {"exit_code", code},
                               {"error", ok ? "" : TailText(run_log_path, 4000)},
                               {"note", note},
                               {"mode", mode},
                               {"via", via},
                               {"f_user", f_user},
                               {"raw_backup", raw_path},
                               {"mapped_backup", mapped_path},
                               {"run_log", run_log_path}};
  if (run_outcome.is_object()) {
    out.payload["run_outcome"] = run_outcome;
  }
  if (ok) {
    out.payload["f_task_result"] = FTaskResultFromMappedDisk(mapped_path);
    out.payload["task_completed"] = TaskCompletedFromMappedDisk(mapped_path);
  }
  out.ok = ok;
  if (lat.Enabled()) {
    const int final_st =
        ok ? (out.payload.value("task_completed", false) ? kStatusDone : kStatusReady) : kStatusFailed;
    (void)lat.FinalizeCsvRow(final_st, code);
  }
  return out;
}

// new_status: ok&&task_completed→3；ok&&!completed→保持 claim_status(2)；!ok→0。
// unlock rate-limit 失败时保持 claim_status 并返回冷却秒数（对齐 Python）。
double FinalizeExecutorTask(TaskRepository *repo, const TaskRow &task, int claim_status,
                            const nlohmann::json &result, bool ok, const std::string &executor_name) {
  if (repo == NULL) {
    return 0.0;
  }
  const std::string err = result.value("error", "");
  const int exit_code = result.value("exit_code", 0);
  bool unlock_requeue = false;
  if (!ok) {
    std::string fatal_code;
    if (result.contains("run_outcome") && result["run_outcome"].is_object()) {
      fatal_code = result["run_outcome"].value("fatal_code", "");
    }
    if (fatal_code == "UNLOCK_RATE_LIMIT" ||
        (exit_code == 2 && trade::IsOrderRateLimitErr(err)) ||
        (ToUpperCopyLocal(err).find("UNLOCK") != std::string::npos && trade::IsOrderRateLimitErr(err))) {
      unlock_requeue = true;
    }
  }

  int new_status = kStatusFailed;
  if (ok) {
    new_status = result.value("task_completed", false) ? kStatusDone : claim_status;
  } else if (unlock_requeue) {
    new_status = claim_status;
  }
  const nlohmann::json f_task_public = ComposeFTaskResultFromWorker(result);
  const bool updated =
      repo->FinalizeExecutor(task, claim_status, new_status, SafeJsonDump(f_task_public));
  if (!updated) {
    repo->InsertTaskEvent(task.f_task_name, 9,
                          "finalize_executor_row_mismatch expected_status=" +
                              std::to_string(claim_status));
    return 0.0;
  }
  if (ok && new_status == kStatusDone) {
    repo->InsertTaskLog(task.f_task_type, task.f_task_name, executor_name, task.f_trader,
                        task.f_task_params, "executor_finish_ok", kStatusDone);
    repo->InsertTaskEvent(task.f_task_name, 6, "executor_finished_ok");
  } else if (ok && new_status == claim_status) {
    repo->InsertTaskLog(task.f_task_type, task.f_task_name, executor_name, task.f_trader,
                        task.f_task_params,
                        "executor_exit_normal_but_not_done_keep_status_" + std::to_string(claim_status),
                        claim_status);
    repo->InsertTaskEvent(task.f_task_name, 6,
                          "executor_finished_keep_status_" + std::to_string(claim_status));
  } else if (unlock_requeue) {
    const std::string fail_snip = err.substr(0, 500);
    repo->InsertTaskLog(task.f_task_type, task.f_task_name, executor_name, task.f_trader,
                        task.f_task_params, "executor_unlock_rate_limit_requeue:" + fail_snip,
                        claim_status);
    repo->InsertTaskEvent(task.f_task_name, 6, "executor_unlock_rate_limit_requeue");
    return kUnlockRateLimitCooldownSec;
  } else {
    const std::string fail_snip = err.substr(0, 500);
    repo->InsertTaskLog(task.f_task_type, task.f_task_name, executor_name, task.f_trader,
                        task.f_task_params, "executor_finish_fail:" + fail_snip, kStatusFailed);
    repo->InsertTaskEvent(task.f_task_name, 6, "executor_finished_fail");
  }
  return 0.0;
}

}  // namespace task
}  // namespace mmtrade
