#pragma once

// Sticky strategy_cpp worker pool: N long-lived processes keep OpenD connected;
// executor dispatches mapped configs over a Unix socket (NDJSON).

#include "task/system_log.hpp"

#include <nlohmann/json.hpp>

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <string>
#include <sys/types.h>
#include <vector>

namespace mmtrade {
namespace task {

struct StrategyPoolRunRequest {
  std::string config_path;
  std::string latency_staging;
  std::string latency_csv;
  std::string run_log;
  int timeout_sec;

  StrategyPoolRunRequest() : timeout_sec(0) {}
};

struct StrategyPoolRunResult {
  bool ok;
  int exit_code;
  bool used_pool;
  std::string error;
  nlohmann::json run_outcome;

  StrategyPoolRunResult() : ok(false), exit_code(1), used_pool(false) {}
};

class StrategyWorkerPool {
 public:
  StrategyWorkerPool();
  ~StrategyWorkerPool();

  bool Start(int size, int task_type, SystemLogger *log);
  void Stop();

  bool enabled() const { return enabled_; }
  int size() const { return worker_count_; }

  StrategyPoolRunResult Run(const StrategyPoolRunRequest &req, SystemLogger *log);

 private:
  struct Worker {
    int id;
    pid_t pid;
    int fd;
    std::string sock_path;
    bool alive;
    bool busy;

    Worker() : id(-1), pid(-1), fd(-1), alive(false), busy(false) {}
  };

  bool SpawnOne(int id, SystemLogger *log);
  bool WaitReady(Worker *w, int timeout_ms, SystemLogger *log);
  bool EnsureWorker(Worker *w, SystemLogger *log);
  void KillWorker(Worker *w);
  StrategyPoolRunResult RunOnWorker(Worker *w, const StrategyPoolRunRequest &req,
                                    SystemLogger *log);

  bool enabled_;
  int task_type_;
  int worker_count_;
  std::string bin_path_;
  std::string sock_dir_;
  std::mutex mu_;
  std::condition_variable cv_;
  std::vector<Worker> workers_;
  size_t rr_;
  SystemLogger *log_;
};

bool StrategyPoolEnabledFromEnv();

}  // namespace task
}  // namespace mmtrade
