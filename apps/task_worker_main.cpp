#include "db/mysql_client.hpp"
#include "db/mysql_config.hpp"
#include "task/system_log.hpp"
#include "task/task_paths.hpp"
#include "task/task_repository.hpp"
#include "task/task_types.hpp"
#include "task/task_worker.hpp"

// task_worker_cpp — Worker 守护进程入口。
//
// 职责：轮询 MySQL strategy_tasks（f_status=1），prepare 后置 f_status=2，供 Executor 执行。
// 启动：strategy/task_system/script/worker.sh
// 默认：poll 10s，f_task_type=2（C++ 专用队列）。

#include <csignal>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

volatile sig_atomic_t g_run = 1;
void OnSignal(int) { g_run = 0; }

struct WorkerArgs {
  std::string mysql_config;
  double poll_sec;
  bool once;
  bool simulate;
  std::string system_log;
  double idle_log_sec;
  int task_type;
};

bool ParseArgs(int argc, char **argv, WorkerArgs *out) {
  if (out == NULL) {
    return false;
  }
  out->poll_sec = 10.0;
  out->once = false;
  out->simulate = false;
  out->idle_log_sec = 60.0;
  out->task_type = mmtrade::task::kCppTaskType;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--mysql-config" && i + 1 < argc) {
      out->mysql_config = argv[++i];
    } else if (a == "--poll-sec" && i + 1 < argc) {
      out->poll_sec = std::atof(argv[++i]);
    } else if (a == "--once") {
      out->once = true;
    } else if (a == "--simulate") {
      out->simulate = true;
    } else if (a == "--system-log" && i + 1 < argc) {
      out->system_log = argv[++i];
    } else if (a == "--idle-log-sec" && i + 1 < argc) {
      out->idle_log_sec = std::atof(argv[++i]);
    } else if (a == "--task-type" && i + 1 < argc) {
      out->task_type = std::atoi(argv[++i]);
    } else if (a == "--help" || a == "-h") {
      std::cout << "usage: task_worker_cpp [--mysql-config PATH] [--poll-sec N] [--once] "
                   "[--simulate] [--task-type 2]\n";
      return false;
    }
  }
  return true;
}

std::string WorkerName() {
  char host[256];
  host[0] = '\0';
  gethostname(host, sizeof(host) - 1);
  return std::string(host) + ":" + std::to_string(static_cast<long long>(std::time(NULL)));
}

}  // namespace

int main(int argc, char **argv) {
  WorkerArgs args;
  if (!ParseArgs(argc, argv, &args)) {
    return 2;
  }
  if (args.system_log.empty()) {
    args.system_log = mmtrade::task::DefaultSystemLogPath();
  }

  mmtrade::task::SystemLogger slog(args.system_log);
  const std::string worker_name = WorkerName();
  slog.Info("worker_start worker=" + worker_name + " task_type=" +
            std::to_string(args.task_type) + " poll_sec=" + std::to_string(args.poll_sec) +
            " simulate=" + (args.simulate ? "1" : "0"));

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
  double idle_last_log = 0.0;

  // 主循环：认领 pending → PrepareTask → FinalizePreparedTask → 释放锁 → sleep(poll_sec)
  while (g_run) {
    if (!db.PingOrReconnect()) {
      slog.Error("mysql_ping_reconnect_failed " + db.LastError());
      usleep(static_cast<useconds_t>(args.poll_sec * 1000000.0));
      continue;
    }
    mmtrade::task::PrepareClaim claim = repo.TryAcquirePrepareSlot(worker_name, args.task_type);
    if (!claim.acquired) {
      const double now = static_cast<double>(std::time(NULL));
      if (now - idle_last_log >= args.idle_log_sec) {
        slog.Info("scan_idle no pending task (f_status=1) task_type=" + std::to_string(args.task_type));
        idle_last_log = now;
      }
      if (args.once) {
        std::cout << "no pending task\n";
        return 0;
      }
      usleep(static_cast<useconds_t>(args.poll_sec * 1000000.0));
      continue;
    }

    const mmtrade::task::PrepareTaskResult prepared =
        mmtrade::task::PrepareTask(claim.task, &db, args.simulate, &slog);
    (void)mmtrade::task::FinalizePreparedTask(&repo, claim.task, prepared.ok, prepared.result,
                                              worker_name, &slog);
    repo.ReleasePrepareLock(claim.lock_key);

    slog.Info("task_prepare_finished f_id=" + std::to_string(static_cast<long long>(claim.task.f_id)) +
              " name=" + claim.task.f_task_name + " ok=" + (prepared.ok ? "1" : "0"));
    std::cout << prepared.result.dump() << std::endl;
    if (args.once) {
      return 0;
    }
  }

  slog.Info("worker_stop worker=" + worker_name);
  return 0;
}
