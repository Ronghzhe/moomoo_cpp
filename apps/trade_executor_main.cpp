#include "db/mysql_client.hpp"
#include "db/mysql_config.hpp"
#include "task/system_log.hpp"
#include "task/task_paths.hpp"
#include "task/task_repository.hpp"
#include "task/task_types.hpp"
#include "task/trade_executor.hpp"
#include "task/strategy_worker_pool.hpp"

// trade_executor_cpp — Executor 守护进程入口。
//
// 对齐 Python trade_executor：线程池并发、市场配额、同标的互斥、not-done/unlock 冷却。

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

volatile sig_atomic_t g_run = 1;
void OnSignal(int) { g_run = 0; }

struct ExecutorArgs {
  std::string mysql_config;
  int task_type;
  int claim_status;
  double poll_sec;
  int timeout_sec;
  bool once;
  std::string pid_file;
  std::string system_log;
  double idle_log_sec;
  double open_stagger_slot_sec;
  int open_stagger_buckets;
  int parallel;
  int parallel_hk;
  int parallel_us;
  double not_done_retry_sec;
};

bool ParseArgs(int argc, char **argv, ExecutorArgs *out) {
  if (out == NULL) {
    return false;
  }
  out->poll_sec = 0.2;
  out->timeout_sec = 0;
  out->once = false;
  out->idle_log_sec = 60.0;
  out->open_stagger_slot_sec = 2.0;
  out->open_stagger_buckets = 20;
  out->parallel = 3;
  out->parallel_hk = 0;
  out->parallel_us = 0;
  out->not_done_retry_sec = 30.0;
  out->task_type = mmtrade::task::kDefaultTaskType;
  out->claim_status = mmtrade::task::kDefaultClaimStatus;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--mysql-config" && i + 1 < argc) {
      out->mysql_config = argv[++i];
    } else if (a == "--task-type" && i + 1 < argc) {
      out->task_type = std::atoi(argv[++i]);
    } else if (a == "--claim-status" && i + 1 < argc) {
      out->claim_status = std::atoi(argv[++i]);
    } else if (a == "--poll-sec" && i + 1 < argc) {
      out->poll_sec = std::atof(argv[++i]);
    } else if (a == "--timeout-sec" && i + 1 < argc) {
      out->timeout_sec = std::atoi(argv[++i]);
    } else if (a == "--parallel" && i + 1 < argc) {
      out->parallel = std::atoi(argv[++i]);
    } else if (a == "--parallel-hk" && i + 1 < argc) {
      out->parallel_hk = std::atoi(argv[++i]);
    } else if (a == "--parallel-us" && i + 1 < argc) {
      out->parallel_us = std::atoi(argv[++i]);
    } else if (a == "--not-done-retry-sec" && i + 1 < argc) {
      out->not_done_retry_sec = std::atof(argv[++i]);
    } else if (a == "--once") {
      out->once = true;
    } else if (a == "--pid-file" && i + 1 < argc) {
      out->pid_file = argv[++i];
    } else if (a == "--system-log" && i + 1 < argc) {
      out->system_log = argv[++i];
    } else if (a == "--idle-log-sec" && i + 1 < argc) {
      out->idle_log_sec = std::atof(argv[++i]);
    } else if (a == "--open-stagger-slot-sec" && i + 1 < argc) {
      out->open_stagger_slot_sec = std::atof(argv[++i]);
    } else if (a == "--open-stagger-buckets" && i + 1 < argc) {
      out->open_stagger_buckets = std::atoi(argv[++i]);
    } else if (a == "--help" || a == "-h") {
      std::cout << "usage: trade_executor_cpp [--task-type N] [--claim-status N] [options]\n"
                << "  --parallel N           max concurrent runs (default: 3)\n"
                << "  --parallel-hk N        max concurrent HK (0=off market split)\n"
                << "  --parallel-us N        max concurrent US (0=off market split)\n"
                << "  --not-done-retry-sec S cooldown when ok but still status=2\n"
                << "  --poll-sec SEC         idle poll (default: 0.2)\n";
      return false;
    }
  }
  if (out->parallel < 1) {
    out->parallel = 1;
  }
  if (out->poll_sec < 0.05) {
    out->poll_sec = 0.05;
  }
  if (out->not_done_retry_sec < 1.0) {
    out->not_done_retry_sec = 1.0;
  }
  return true;
}

std::string ExecutorName() {
  char host[256];
  host[0] = '\0';
  gethostname(host, sizeof(host) - 1);
  return std::string("executor_cpp:") + host + ":" +
         std::to_string(static_cast<long long>(std::time(NULL)));
}

void WritePidFile(const std::string &path) {
  if (path.empty()) {
    return;
  }
  std::ofstream ofs(path.c_str());
  ofs << getpid() << "\n";
}

struct WorkerJob {
  std::thread th;
  std::atomic<bool> done;
  int64_t f_id;
  std::string task_name;
  std::string market;
  std::string symbol;
  bool ok;
  bool task_completed;
  double cooldown_sec;

  WorkerJob()
      : done(false),
        f_id(0),
        ok(false),
        task_completed(false),
        cooldown_sec(0.0) {}
};

}  // namespace

int main(int argc, char **argv) {
  ExecutorArgs args;
  if (!ParseArgs(argc, argv, &args)) {
    return 2;
  }
  if (args.system_log.empty()) {
    args.system_log = mmtrade::task::DefaultSystemLogPath();
  }
  WritePidFile(args.pid_file);

  std::map<std::string, int> market_limits;
  if (args.parallel_hk > 0 || args.parallel_us > 0) {
    market_limits["HK"] = args.parallel_hk > 0 ? args.parallel_hk : args.parallel;
    market_limits["US"] = args.parallel_us > 0 ? args.parallel_us : args.parallel;
  }
  const bool symbol_exclusive = mmtrade::task::EntryNetTestEnabled();

  mmtrade::task::SystemLogger slog(args.system_log);
  const std::string executor_name = ExecutorName();
  {
    std::ostringstream oss;
    oss << "executor_daemon_boot task_type=" << args.task_type
        << " claim_status=" << args.claim_status << " parallel=" << args.parallel
        << " poll_sec=" << args.poll_sec << " not_done_retry_sec=" << args.not_done_retry_sec
        << " symbol_exclusive=" << (symbol_exclusive ? "1" : "0");
    if (!market_limits.empty()) {
      oss << " market_limits=HK:" << market_limits["HK"] << ",US:" << market_limits["US"];
    } else {
      oss << " market_limits=none";
    }
    oss << " backup_dir=" << mmtrade::task::DefaultTaskBackupDir();
    slog.Info(oss.str());
  }

  mmtrade::db::MysqlConfig cfg = mmtrade::db::LoadMysqlConfig(args.mysql_config);
  cfg.database = mmtrade::db::DefaultStrategyTradeDatabase();
  mmtrade::db::MysqlClient db(cfg);
  if (!db.Connect()) {
    slog.Error("mysql_connect_failed " + db.LastError());
    return 1;
  }

  std::signal(SIGINT, OnSignal);
  std::signal(SIGTERM, OnSignal);

  mmtrade::task::TaskRepository repo(&db);
  mmtrade::task::StrategyWorkerPool strategy_pool;
  if (mmtrade::task::StrategyPoolEnabledFromEnv()) {
    if (!strategy_pool.Start(args.parallel, args.task_type, &slog)) {
      slog.Info("strategy_pool_disabled_fallback_shell");
    }
  } else {
    slog.Info("strategy_pool_env_off using_shell");
  }
  std::mutex claimed_mu;
  std::set<int64_t> claimed_ids;
  std::mutex jobs_mu;
  std::vector<std::shared_ptr<WorkerJob> > jobs;
  std::mutex block_mu;
  std::map<int64_t, double> blocked_until;
  double idle_last = 0.0;

  auto reap_done = [&]() {
    std::vector<std::shared_ptr<WorkerJob> > finished;
    {
      std::lock_guard<std::mutex> lk(jobs_mu);
      std::vector<std::shared_ptr<WorkerJob> > keep;
      for (size_t i = 0; i < jobs.size(); ++i) {
        if (jobs[i]->done.load()) {
          finished.push_back(jobs[i]);
        } else {
          keep.push_back(jobs[i]);
        }
      }
      jobs.swap(keep);
    }
    const double now = static_cast<double>(std::time(NULL));
    for (size_t i = 0; i < finished.size(); ++i) {
      if (finished[i]->th.joinable()) {
        finished[i]->th.join();
      }
      {
        std::lock_guard<std::mutex> lk(claimed_mu);
        claimed_ids.erase(finished[i]->f_id);
      }
      if (finished[i]->ok && !finished[i]->task_completed) {
        std::lock_guard<std::mutex> lk(block_mu);
        blocked_until[finished[i]->f_id] = now + args.not_done_retry_sec;
        slog.Info("executor_not_done_cooldown f_id=" +
                  std::to_string(static_cast<long long>(finished[i]->f_id)) +
                  " task=" + finished[i]->task_name +
                  " keep_status=2 retry_in=" + std::to_string(args.not_done_retry_sec) + "s");
      } else if (finished[i]->cooldown_sec > 0.0) {
        std::lock_guard<std::mutex> lk(block_mu);
        blocked_until[finished[i]->f_id] = now + finished[i]->cooldown_sec;
        slog.Info("executor_unlock_rate_limit_cooldown f_id=" +
                  std::to_string(static_cast<long long>(finished[i]->f_id)) +
                  " task=" + finished[i]->task_name +
                  " retry_in=" + std::to_string(finished[i]->cooldown_sec) + "s");
      } else {
        std::lock_guard<std::mutex> lk(block_mu);
        blocked_until.erase(finished[i]->f_id);
      }
      slog.Info("executor_done f_id=" +
                std::to_string(static_cast<long long>(finished[i]->f_id)) +
                " task=" + finished[i]->task_name + " ok=" + (finished[i]->ok ? "1" : "0"));
    }
  };

  auto inflight = [&]() -> int {
    std::lock_guard<std::mutex> lk(jobs_mu);
    return static_cast<int>(jobs.size());
  };

  auto market_in_flight = [&]() -> std::map<std::string, int> {
    std::map<std::string, int> counts;
    counts["HK"] = 0;
    counts["US"] = 0;
    std::lock_guard<std::mutex> lk(jobs_mu);
    for (size_t i = 0; i < jobs.size(); ++i) {
      const std::string &m = jobs[i]->market;
      counts[m] = counts[m] + 1;
    }
    return counts;
  };

  auto busy_symbols = [&]() -> std::set<std::string> {
    std::set<std::string> out;
    if (!symbol_exclusive) {
      return out;
    }
    std::lock_guard<std::mutex> lk(jobs_mu);
    for (size_t i = 0; i < jobs.size(); ++i) {
      if (!jobs[i]->symbol.empty()) {
        out.insert(jobs[i]->symbol);
      }
    }
    return out;
  };

  while (g_run) {
    reap_done();

    if (inflight() >= args.parallel) {
      usleep(static_cast<useconds_t>(args.poll_sec * 1000000.0));
      continue;
    }

    if (!db.PingOrReconnect()) {
      slog.Error("mysql_ping_reconnect_failed " + db.LastError());
      usleep(static_cast<useconds_t>(args.poll_sec * 1000000.0));
      continue;
    }

    std::set<int64_t> exclude;
    {
      std::lock_guard<std::mutex> lk(claimed_mu);
      exclude = claimed_ids;
    }
    {
      const double now = static_cast<double>(std::time(NULL));
      std::lock_guard<std::mutex> lk(block_mu);
      for (std::map<int64_t, double>::iterator it = blocked_until.begin();
           it != blocked_until.end();) {
        if (it->second <= now) {
          std::map<int64_t, double>::iterator erase_it = it++;
          blocked_until.erase(erase_it);
        } else {
          exclude.insert(it->first);
          ++it;
        }
      }
    }

    mmtrade::task::ExecutorClaim claim = repo.ClaimExecutorTask(
        executor_name, args.task_type, args.claim_status, exclude, market_limits,
        market_in_flight(), busy_symbols(), &slog);
    if (!claim.acquired) {
      const double now = static_cast<double>(std::time(NULL));
      if (inflight() == 0) {
        if (now - idle_last >= args.idle_log_sec) {
          slog.Info("scan_idle no task f_status=" + std::to_string(args.claim_status) +
                    " task_type=" + std::to_string(args.task_type));
          idle_last = now;
        }
        if (args.once) {
          std::cout << "no task at f_status=" << args.claim_status << "\n";
          return 0;
        }
      }
      usleep(static_cast<useconds_t>(args.poll_sec * 1000000.0));
      continue;
    }

    {
      std::lock_guard<std::mutex> lk(claimed_mu);
      claimed_ids.insert(claim.task.f_id);
    }

    std::shared_ptr<WorkerJob> job(new WorkerJob());
    job->f_id = claim.task.f_id;
    job->task_name = claim.task.f_task_name;
    job->market = mmtrade::task::InferTaskMarket(claim.task);
    job->symbol = mmtrade::task::InferTaskSymbol(claim.task);
    const mmtrade::task::TaskRow task = claim.task;
    const int timeout_sec = args.timeout_sec;
    const int claim_status = args.claim_status;
    const double stagger_slot = args.open_stagger_slot_sec;
    const int stagger_buckets = args.open_stagger_buckets;
    const std::string mysql_config = args.mysql_config;
    const std::string exec_name = executor_name;

    job->th = std::thread([job, task, timeout_sec, claim_status, stagger_slot, stagger_buckets,
                           mysql_config, exec_name, &slog, &strategy_pool]() {
      mmtrade::db::MysqlConfig wcfg = mmtrade::db::LoadMysqlConfig(mysql_config);
      wcfg.database = mmtrade::db::DefaultStrategyTradeDatabase();
      mmtrade::db::MysqlClient wdb(wcfg);
      if (!wdb.Connect()) {
        slog.Error("worker_mysql_connect_failed f_id=" +
                   std::to_string(static_cast<long long>(task.f_id)) + " " + wdb.LastError());
        job->ok = false;
        job->done.store(true);
        return;
      }
      mmtrade::task::TaskRepository wrepo(&wdb);
      mmtrade::task::StrategyWorkerPool *pool_ptr =
          strategy_pool.enabled() ? &strategy_pool : NULL;
      const mmtrade::task::ExecutorRunResult run = mmtrade::task::RunOneTask(
          task, timeout_sec, &slog, stagger_slot, stagger_buckets, pool_ptr);
      job->cooldown_sec = mmtrade::task::FinalizeExecutorTask(
          &wrepo, task, claim_status, run.payload, run.ok, exec_name);
      job->ok = run.ok;
      job->task_completed = run.payload.value("task_completed", false);
      try {
        std::cout << run.payload.dump() << std::endl;
      } catch (const std::exception &e) {
        slog.Error("executor_payload_dump_failed task=" + task.f_task_name + " err=" + e.what());
      }
      job->done.store(true);
    });

    {
      std::lock_guard<std::mutex> lk(jobs_mu);
      jobs.push_back(job);
    }

    if (args.once) {
      while (!job->done.load() && g_run) {
        usleep(50000);
      }
      reap_done();
      return 0;
    }
  }

  while (inflight() > 0) {
    usleep(100000);
    reap_done();
  }
  strategy_pool.Stop();
  slog.Info("executor_stop " + executor_name);
  return 0;
}
