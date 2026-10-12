#include "task/task_latency_recorder.hpp"

#include "task/task_paths.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <queue>
#include <sstream>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace mmtrade {
namespace task {

namespace {

std::string JoinPath(const std::string &a, const std::string &b) {
  if (a.empty()) {
    return b;
  }
  if (a.back() == '/') {
    return a + b;
  }
  return a + "/" + b;
}

bool EnsureDir(const std::string &path) {
  if (path.empty()) {
    return false;
  }
  std::string built;
  for (size_t i = 0; i < path.size(); ++i) {
    built.push_back(path[i]);
    if (path[i] == '/' && built.size() > 1) {
      mkdir(built.c_str(), 0755);
    }
  }
  return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

int64_t JsonToInt64(const nlohmann::json &j) {
  if (j.is_null()) {
    return 0;
  }
  if (j.is_number_integer()) {
    return j.get<int64_t>();
  }
  if (j.is_number_float()) {
    return static_cast<int64_t>(j.get<double>());
  }
  if (j.is_string()) {
    try {
      return std::stoll(j.get<std::string>());
    } catch (...) {
      return 0;
    }
  }
  return 0;
}

std::string CsvEscape(const std::string &s) {
  if (s.find_first_of(",\"\n\r") == std::string::npos) {
    return s;
  }
  std::string out = "\"";
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '"') {
      out += "\"\"";
    } else {
      out.push_back(s[i]);
    }
  }
  out += "\"";
  return out;
}

const std::vector<std::string> &AllCsvColumns() {
  static const std::vector<std::string> kCols = {
      "batch_id",                 "f_id",                     "task_name",
      "f_task_type",              "symbol",                   "stock_code",
      "trade_qty",                "final_status",             "exit_code",
      "insert_ts_ms",             "prepare_acquire_ms",       "prepare_parse_done_ms",
      "prepare_account_done_ms",  "prepare_build_done_ms",    "prepare_backup_done_ms",
      "prepare_done_ms",          "prepare_status_2_ms",
      "ms_prepare_acquire_to_parse", "ms_prepare_parse_to_account",
      "ms_prepare_account_to_build", "ms_prepare_build_to_backup",
      "ms_prepare_backup_to_done", "ms_prepare_total",
      "executor_claim_ms",        "mapped_ready_ms",
      "shell_start_ms",           "shell_end_ms",             "executor_finalize_ms",
      "strategy_main_start_ms",   "config_loaded_ms",         "opend_connected_ms",
      "subscribe_done_ms",        "order_client_ready_ms",    "first_book_push_ms",
      "tick_count",               "first_judge_ms",           "entry_judge_hit_ms",
      "exit_judge_hit_ms",        "live_eval_first_ms",       "judge_total_count",
      "entry_place_start_ms",     "entry_place_api_ms",       "entry_place_sent_ms",
      "entry_place_pre_us",       "entry_place_sdk_us",       "entry_place_post_us",
      "entry_gateway_queue_wait_us", "entry_gateway_rate_wait_us",
      "entry_gateway_http_rtt_us", "entry_place_via",
      "entry_price_compute_us",   "entry_push_first_ms",      "entry_poll_first_ms",
      "entry_push_submitting_ms", "entry_push_filled_ms",     "entry_filled_ms",
      "entry_apply_fill_ms",      "entry_fill_source",        "entry_cancel_count",
      "entry_replace_count",      "exit_place_start_ms",      "exit_place_api_ms",
      "exit_place_sent_ms",       "exit_place_pre_us",        "exit_place_sdk_us",
      "exit_place_post_us",
      "exit_gateway_queue_wait_us", "exit_gateway_rate_wait_us",
      "exit_gateway_http_rtt_us", "exit_place_via",
      "exit_push_first_ms",       "exit_poll_first_ms",
      "exit_push_submitting_ms",  "exit_push_filled_ms",      "exit_filled_ms",
      "exit_apply_fill_ms",       "exit_fill_source",         "exit_cancel_count",
      "exit_replace_count",       "strategy_exit_ms",         "ms_insert_to_ready",
      "ms_ready_to_executor",     "ms_executor_to_shell_end", "ms_shell_start_to_end",
      "ms_insert_to_first_push",  "ms_entry_hit_to_filled",   "ms_exit_hit_to_filled",
      "ms_insert_to_done",        "us_hit_to_place_sent",     "us_place_sent_to_push",
      "us_place_sent_to_filled",  "us_push_to_filled",        "us_exit_hit_to_place_sent",
      "us_exit_place_sent_to_filled",
  };
  return kCols;
}

class FileLockGuard {
 public:
  explicit FileLockGuard(int fd) : fd_(fd), locked_(false) {
    if (fd_ >= 0) {
      locked_ = (flock(fd_, LOCK_EX) == 0);
    }
  }
  ~FileLockGuard() {
    if (locked_ && fd_ >= 0) {
      flock(fd_, LOCK_UN);
    }
  }

 private:
  int fd_;
  bool locked_;
};

int64_t WallMsNowLocal() {
  const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

int64_t WallUsNowLocal() {
  const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
  return std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
}

void PutDeltaMs(nlohmann::json *data, const char *field, int64_t start_ms, int64_t end_ms) {
  if (data == NULL || field == NULL || start_ms <= 0 || end_ms <= 0) {
    return;
  }
  (*data)[field] = end_ms - start_ms;
}

void PutDeltaUsFromMs(nlohmann::json *data, const char *field, int64_t start_ms, int64_t end_ms) {
  if (data == NULL || field == NULL || start_ms <= 0 || end_ms <= 0) {
    return;
  }
  (*data)[field] = (end_ms - start_ms) * 1000;
}

std::string SymbolDirTag(const std::string &symbol) {
  std::string out;
  for (size_t i = 0; i < symbol.size(); ++i) {
    out.push_back(symbol[i] == '.' ? '_' : symbol[i]);
  }
  return out;
}

std::string ResolveLatencySymbol(const nlohmann::json &snap) {
  if (snap.contains("symbol") && snap["symbol"].is_string()) {
    const std::string sym = snap["symbol"].get<std::string>();
    if (!sym.empty()) {
      return sym;
    }
  }
  std::string stock_type;
  std::string stock_code;
  if (snap.contains("stock_type") && snap["stock_type"].is_string()) {
    stock_type = snap["stock_type"].get<std::string>();
  }
  if (snap.contains("stock_code") && snap["stock_code"].is_string()) {
    stock_code = snap["stock_code"].get<std::string>();
  }
  if (stock_type.empty() && snap.contains("f_task_type")) {
    const int64_t tt = JsonToInt64(snap["f_task_type"]);
    if (tt == 4) {
      stock_type = "HK";
    }
  }
  if (!stock_type.empty() && !stock_code.empty()) {
    return stock_type + "." + stock_code;
  }
  return "";
}

std::string DailySymbolLatencyCsvPath(const nlohmann::json &snap) {
  const std::string symbol = ResolveLatencySymbol(snap);
  if (symbol.empty()) {
    return "";
  }
  std::string csv_dir;
  if (snap.contains("latency_csv_dir") && snap["latency_csv_dir"].is_string()) {
    csv_dir = snap["latency_csv_dir"].get<std::string>();
  }
  if (csv_dir.empty()) {
    csv_dir = JoinPath(DefaultStrategyRoot(), "daily_logs");
  }
  const int64_t acc_id = JsonToInt64(snap.value("acc_id", 0));
  const int64_t acc_index = JsonToInt64(snap.value("acc_index", 0));
  std::ostringstream account;
  account << "acc_" << acc_id << "_idx_" << acc_index;
  return JoinPath(JoinPath(JoinPath(csv_dir, account.str()), SymbolDirTag(symbol)), "latency.csv");
}

std::string PerTaskLatencyCsvPath(const nlohmann::json &snap) {
  if (!snap.contains("f_user") || !snap["f_user"].is_string()) {
    return "";
  }
  if (!snap.contains("task_name") || !snap["task_name"].is_string()) {
    return "";
  }
  const std::string f_user = snap["f_user"].get<std::string>();
  const std::string task_name = snap["task_name"].get<std::string>();
  if (f_user.empty() || task_name.empty()) {
    return "";
  }
  return JoinPath(DefaultTaskSystemDir(), "run_logs/" + SafePathSegment(f_user) + "/" +
                                              SafePathSegment(task_name, "_noname") +
                                              ".task_latency.csv");
}

bool AppendLatencyCsvRowLocked(const std::string &csv, const nlohmann::json &snap) {
  if (csv.empty()) {
    return false;
  }
  const size_t slash = csv.find_last_of('/');
  if (slash != std::string::npos) {
    EnsureDir(csv.substr(0, slash));
  }

  const int lock_fd = ::open((csv + ".lock").c_str(), O_CREAT | O_RDWR, 0644);
  bool ok_write = false;
  {
    FileLockGuard file_lk(lock_fd);
    const std::vector<std::string> &cols = AllCsvColumns();
    bool need_header = access(csv.c_str(), F_OK) != 0;
    if (!need_header) {
      std::ifstream ifs(csv.c_str());
      std::string existing_header;
      if (std::getline(ifs, existing_header)) {
        std::ostringstream expect;
        for (size_t i = 0; i < cols.size(); ++i) {
          if (i > 0) {
            expect << ',';
          }
          expect << cols[i];
        }
        if (existing_header != expect.str()) {
          const std::string rotated =
              csv + ".prev_" + std::to_string(static_cast<long long>(WallMsNowLocal()));
          ::rename(csv.c_str(), rotated.c_str());
          need_header = true;
        }
      } else {
        need_header = true;
      }
    }
    std::ofstream ofs(csv.c_str(), std::ios::app);
    if (ofs) {
      if (need_header) {
        for (size_t i = 0; i < cols.size(); ++i) {
          if (i > 0) {
            ofs << ',';
          }
          ofs << cols[i];
        }
        ofs << '\n';
      }
      for (size_t i = 0; i < cols.size(); ++i) {
        if (i > 0) {
          ofs << ',';
        }
        if (snap.contains(cols[i]) && !snap[cols[i]].is_null()) {
          if (snap[cols[i]].is_string()) {
            ofs << CsvEscape(snap[cols[i]].get<std::string>());
          } else if (snap[cols[i]].is_number()) {
            ofs << JsonToInt64(snap[cols[i]]);
          } else {
            ofs << CsvEscape(snap[cols[i]].dump());
          }
        }
      }
      ofs << '\n';
      ok_write = true;
    }
  }
  if (lock_fd >= 0) {
    ::close(lock_fd);
  }
  return ok_write;
}

}  // namespace

struct TaskLatencyRecorder::Impl {
  std::string staging_path;
  std::string csv_path;
  bool enabled;

  std::mutex queue_mu;
  std::condition_variable queue_cv;
  std::queue<std::pair<std::string, int64_t> > set_queue;
  std::queue<std::pair<std::string, std::string> > str_queue;
  std::queue<std::string> inc_queue;
  bool stop;
  bool flush_requested;

  std::mutex data_mu;
  nlohmann::json data;

  std::thread worker;

  Impl() : enabled(false), stop(false), flush_requested(false) {
    worker = std::thread([this]() { WorkerLoop(); });
  }

  ~Impl() {
    {
      std::lock_guard<std::mutex> lk(queue_mu);
      stop = true;
      flush_requested = true;
    }
    queue_cv.notify_all();
    if (worker.joinable()) {
      worker.join();
    }
  }

  void WorkerLoop() {
    for (;;) {
      std::unique_lock<std::mutex> lk(queue_mu);
      queue_cv.wait(lk, [this]() {
        return stop || !set_queue.empty() || !str_queue.empty() || !inc_queue.empty() ||
               flush_requested;
      });
      if (stop && set_queue.empty() && str_queue.empty() && inc_queue.empty() && !flush_requested) {
        break;
      }
      std::queue<std::pair<std::string, int64_t> > sets;
      std::queue<std::pair<std::string, std::string> > strs;
      std::queue<std::string> incs;
      sets.swap(set_queue);
      strs.swap(str_queue);
      incs.swap(inc_queue);
      const bool do_flush = flush_requested;
      flush_requested = false;
      lk.unlock();

      {
        std::lock_guard<std::mutex> dk(data_mu);
        while (!sets.empty()) {
          const std::pair<std::string, int64_t> op = sets.front();
          sets.pop();
          data[op.first] = op.second;
        }
        while (!strs.empty()) {
          const std::pair<std::string, std::string> op = strs.front();
          strs.pop();
          data[op.first] = op.second;
        }
        while (!incs.empty()) {
          const std::string key = incs.front();
          incs.pop();
          const int64_t cur = JsonToInt64(data.value(key, 0));
          data[key] = cur + 1;
        }
      }

      if (do_flush || !sets.empty() || !strs.empty() || !incs.empty()) {
        (void)PersistStaging();
      }
      if (stop) {
        break;
      }
    }
  }

  bool ReadStagingFile(nlohmann::json *out) const {
    if (out == NULL || staging_path.empty()) {
      return false;
    }
    std::ifstream ifs(staging_path.c_str());
    if (!ifs) {
      return false;
    }
    try {
      nlohmann::json j;
      ifs >> j;
      if (!j.is_object()) {
        return false;
      }
      *out = j;
      return true;
    } catch (...) {
      return false;
    }
  }

  bool LoadStagingFromDisk() {
    nlohmann::json j;
    if (!ReadStagingFile(&j)) {
      return false;
    }
    std::lock_guard<std::mutex> lk(data_mu);
    for (nlohmann::json::iterator it = j.begin(); it != j.end(); ++it) {
      data[it.key()] = it.value();
    }
    return true;
  }

  bool PersistStaging() {
    if (!enabled || staging_path.empty()) {
      return false;
    }
    const size_t slash = staging_path.find_last_of('/');
    if (slash != std::string::npos) {
      EnsureDir(staging_path.substr(0, slash));
    }
    nlohmann::json disk;
    (void)ReadStagingFile(&disk);
    nlohmann::json snapshot = disk;
    {
      std::lock_guard<std::mutex> lk(data_mu);
      for (nlohmann::json::iterator it = data.begin(); it != data.end(); ++it) {
        snapshot[it.key()] = it.value();
      }
    }
    const std::string tmp = staging_path + ".tmp";
    {
      std::ofstream ofs(tmp.c_str(), std::ios::trunc);
      if (!ofs) {
        return false;
      }
      ofs << snapshot.dump();
    }
    rename(tmp.c_str(), staging_path.c_str());
    return true;
  }

  void EnqueueSet(const std::string &field, int64_t value) {
    if (!enabled) {
      return;
    }
    {
      std::lock_guard<std::mutex> lk(queue_mu);
      set_queue.push(std::make_pair(field, value));
    }
    queue_cv.notify_one();
  }

  void EnqueueStr(const std::string &field, const std::string &value) {
    if (!enabled) {
      return;
    }
    {
      std::lock_guard<std::mutex> lk(queue_mu);
      str_queue.push(std::make_pair(field, value));
    }
    queue_cv.notify_one();
  }

  void EnqueueInc(const std::string &field) {
    if (!enabled) {
      return;
    }
    {
      std::lock_guard<std::mutex> lk(queue_mu);
      inc_queue.push(field);
    }
    queue_cv.notify_one();
  }

  void FlushAndWaitImpl() {
    if (!enabled) {
      return;
    }
    {
      std::lock_guard<std::mutex> lk(queue_mu);
      flush_requested = true;
    }
    queue_cv.notify_all();
    for (int i = 0; i < 200; ++i) {
      {
        std::lock_guard<std::mutex> lk(queue_mu);
        if (set_queue.empty() && str_queue.empty() && inc_queue.empty() && !flush_requested) {
          break;
        }
      }
      usleep(5000);
    }
    (void)PersistStaging();
  }

  void ComputeDeltas() {
    std::lock_guard<std::mutex> lk(data_mu);
    const int64_t insert = JsonToInt64(data.value("insert_ts_ms", 0));
    const int64_t ready = JsonToInt64(data.value("prepare_status_2_ms", 0));
    const int64_t claim = JsonToInt64(data.value("executor_claim_ms", 0));
    const int64_t shell_start = JsonToInt64(data.value("shell_start_ms", 0));
    const int64_t shell_end = JsonToInt64(data.value("shell_end_ms", 0));
    const int64_t first_push = JsonToInt64(data.value("first_book_push_ms", 0));
    const int64_t entry_hit = JsonToInt64(data.value("entry_judge_hit_ms", 0));
    const int64_t exit_hit = JsonToInt64(data.value("exit_judge_hit_ms", 0));
    const int64_t entry_sent = JsonToInt64(data.value("entry_place_sent_ms", 0));
    const int64_t entry_push = JsonToInt64(data.value("entry_push_first_ms", 0));
    const int64_t entry_push_fill = JsonToInt64(data.value("entry_push_filled_ms", 0));
    const int64_t entry_fill = JsonToInt64(data.value("entry_filled_ms", 0));
    const int64_t exit_sent = JsonToInt64(data.value("exit_place_sent_ms", 0));
    const int64_t exit_fill = JsonToInt64(data.value("exit_filled_ms", 0));
    const int64_t finalize = JsonToInt64(data.value("executor_finalize_ms", 0));

    PutDeltaMs(&data, "ms_insert_to_ready", insert, ready);
    PutDeltaMs(&data, "ms_ready_to_executor", ready, claim);
    PutDeltaMs(&data, "ms_executor_to_shell_end", claim, shell_end);
    PutDeltaMs(&data, "ms_shell_start_to_end", shell_start, shell_end);
    PutDeltaMs(&data, "ms_insert_to_first_push", insert, first_push);
    PutDeltaMs(&data, "ms_entry_hit_to_filled", entry_hit, entry_fill);
    PutDeltaMs(&data, "ms_exit_hit_to_filled", exit_hit, exit_fill);
    PutDeltaMs(&data, "ms_insert_to_done", insert, finalize);

    const int64_t prep_acq = JsonToInt64(data.value("prepare_acquire_ms", 0));
    const int64_t prep_parse = JsonToInt64(data.value("prepare_parse_done_ms", 0));
    const int64_t prep_acct = JsonToInt64(data.value("prepare_account_done_ms", 0));
    const int64_t prep_build = JsonToInt64(data.value("prepare_build_done_ms", 0));
    const int64_t prep_backup = JsonToInt64(data.value("prepare_backup_done_ms", 0));
    const int64_t prep_done = JsonToInt64(data.value("prepare_done_ms", 0));
    PutDeltaMs(&data, "ms_prepare_acquire_to_parse", prep_acq, prep_parse);
    PutDeltaMs(&data, "ms_prepare_parse_to_account", prep_parse, prep_acct);
    PutDeltaMs(&data, "ms_prepare_account_to_build", prep_acct, prep_build);
    PutDeltaMs(&data, "ms_prepare_build_to_backup", prep_build, prep_backup);
    PutDeltaMs(&data, "ms_prepare_backup_to_done", prep_backup, prep_done);
    PutDeltaMs(&data, "ms_prepare_total", prep_acq, ready > 0 ? ready : prep_done);

    PutDeltaUsFromMs(&data, "us_hit_to_place_sent", entry_hit, entry_sent);
    PutDeltaUsFromMs(&data, "us_place_sent_to_push", entry_sent, entry_push);
    PutDeltaUsFromMs(&data, "us_place_sent_to_filled", entry_sent, entry_fill);
    PutDeltaUsFromMs(&data, "us_push_to_filled", entry_push_fill > 0 ? entry_push_fill : entry_push,
                     entry_fill);
    PutDeltaUsFromMs(&data, "us_exit_hit_to_place_sent", exit_hit, exit_sent);
    PutDeltaUsFromMs(&data, "us_exit_place_sent_to_filled", exit_sent, exit_fill);
  }

  bool FinalizeCsvRowImpl(int final_status, int exit_code) {
    FlushAndWaitImpl();
    (void)LoadStagingFromDisk();
    {
      std::lock_guard<std::mutex> lk(data_mu);
      data["final_status"] = final_status;
      data["exit_code"] = exit_code;
      data["executor_finalize_ms"] = WallMsNowLocal();
    }
    ComputeDeltas();
    (void)PersistStaging();

    nlohmann::json snap;
    {
      std::lock_guard<std::mutex> lk(data_mu);
      snap = data;
    }

    const std::string csv = csv_path.empty() ? TaskLatencyRecorder::DefaultCsvPath() : csv_path;
    const std::string daily_symbol_csv = DailySymbolLatencyCsvPath(snap);

    // Serialize CSV append across executor worker threads / processes.
    static std::mutex g_csv_mu;
    std::lock_guard<std::mutex> csv_lk(g_csv_mu);
    const bool ok_global = AppendLatencyCsvRowLocked(csv, snap);
    if (!ok_global) {
      return false;
    }
    if (!daily_symbol_csv.empty() && daily_symbol_csv != csv) {
      (void)AppendLatencyCsvRowLocked(daily_symbol_csv, snap);
    }
    if (!staging_path.empty()) {
      unlink(staging_path.c_str());
    }
    return true;
  }
};

TaskLatencyRecorder::TaskLatencyRecorder() : impl_(new Impl()) {}

TaskLatencyRecorder::~TaskLatencyRecorder() {
  delete impl_;
  impl_ = NULL;
}

TaskLatencyRecorder &TaskLatencyRecorder::Instance() {
  static TaskLatencyRecorder inst;
  return inst;
}

void TaskLatencyRecorder::Configure(const std::string &staging_path, const std::string &csv_path) {
  {
    std::lock_guard<std::mutex> lk(impl_->data_mu);
    impl_->data = nlohmann::json::object();
  }
  impl_->staging_path = staging_path;
  impl_->csv_path = csv_path;
  impl_->enabled = !staging_path.empty();
  if (impl_->enabled) {
    (void)impl_->LoadStagingFromDisk();
  }
}

bool TaskLatencyRecorder::Enabled() const { return impl_->enabled; }

void TaskLatencyRecorder::RecordTimestampMs(const char *field) {
  if (field == NULL || !impl_->enabled) {
    return;
  }
  impl_->EnqueueSet(field, WallMsNow());
}

void TaskLatencyRecorder::RecordValue(const char *field, int64_t value) {
  if (field == NULL || !impl_->enabled) {
    return;
  }
  impl_->EnqueueSet(field, value);
}

void TaskLatencyRecorder::RecordString(const char *field, const std::string &value) {
  if (field == NULL || !impl_->enabled) {
    return;
  }
  impl_->EnqueueStr(field, value);
}

void TaskLatencyRecorder::Increment(const char *field) {
  if (field == NULL || !impl_->enabled) {
    return;
  }
  impl_->EnqueueInc(field);
}

void TaskLatencyRecorder::FlushAndWait() { impl_->FlushAndWaitImpl(); }

bool TaskLatencyRecorder::FinalizeCsvRow(int final_status, int exit_code) {
  return impl_->FinalizeCsvRowImpl(final_status, exit_code);
}

int64_t TaskLatencyRecorder::WallMsNow() { return WallMsNowLocal(); }

int64_t TaskLatencyRecorder::WallUsNow() { return WallUsNowLocal(); }

std::string TaskLatencyRecorder::DefaultCsvPath() {
  return JoinPath(DefaultTaskSystemDir(), "bench_logs/task_latency.csv");
}

std::string TaskLatencyRecorder::DefaultStagingDir() {
  return JoinPath(DefaultTaskSystemDir(), "bench_logs/staging");
}

std::string TaskLatencyRecorder::StagingPath(int64_t f_id, const std::string &task_name) {
  return JoinPath(DefaultStagingDir(), std::to_string(static_cast<long long>(f_id)) + "_" +
                                           SafePathSegment(task_name, "task") + ".json");
}

const std::vector<std::string> &TaskLatencyRecorder::CsvColumns() { return AllCsvColumns(); }

void TaskLatencyRecorder::ConfigureFromEnv() {
  const char *staging = std::getenv("MOOMOO_TASK_LATENCY_STAGING");
  const char *csv = std::getenv("MOOMOO_TASK_LATENCY_CSV");
  if (staging != NULL && staging[0] != '\0') {
    Instance().Configure(staging, csv != NULL ? std::string(csv) : DefaultCsvPath());
  }
}

}  // namespace task
}  // namespace mmtrade
