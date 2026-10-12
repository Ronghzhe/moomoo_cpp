#pragma once

// 任务队列公共类型：strategy_tasks 表行、状态码、Worker/Executor 认领结果。
//
// 典型生命周期（两进程串行）：
//   INSERT f_status=1 (pending)
//     → task_worker_cpp: prepare → f_status=2 (ready)
//     → trade_executor_cpp: run strategy_cpp → f_status=3 (done) 或 0 (failed)
//
// 详见 task_repository / task_worker / trade_executor 各模块注释。

#include <cstdint>
#include <map>
#include <string>

namespace mmtrade {
namespace task {

// strategy_tasks.f_status
const int kStatusFailed = 0;   // prepare 或 execute 失败
const int kStatusPending = 1;  // 待 Worker 认领并 prepare
const int kStatusReady = 2;    // prepare 完成，待 Executor 拉起 strategy_cpp
const int kStatusDone = 3;     // 策略跑完且 task_completed=true

// Python 生产任务默认 f_task_type=1；C++ 专用队列 f_task_type=2（worker.sh 默认）。
const int kDefaultTaskType = 1;
const int kCppTaskType = 2;

// Executor 默认认领 Worker prepare 后的 f_status=2；可用 --claim-status 跳过 Worker 直接从 1 认领。
const int kDefaultClaimStatus = 2;

// strategy_tasks 表一行（Worker/Executor 共用）。
struct TaskRow {
  int64_t f_id;
  std::string f_task_name;
  int f_task_type;
  std::string f_user;
  std::string f_trader;
  std::string f_task_params;
  std::string f_updatetime;
  int f_status;

  TaskRow();
};

// Worker 成功抢到 prepare 槽位时的返回值；acquired=false 表示本轮无任务或被其他 Worker 占用。
struct PrepareClaim {
  TaskRow task;
  std::string lock_key;  // MySQL GET_LOCK 键，prepare 结束后须 ReleasePrepareLock
  bool acquired;

  PrepareClaim();
};

// Executor 成功选中一条待执行任务时的返回值。
struct ExecutorClaim {
  TaskRow task;
  bool acquired;

  ExecutorClaim();
};

// 生成 Worker prepare 用的 MySQL 锁名：stp_<f_id>。
std::string PrepareLockKey(int64_t f_id);

// 将 MysqlClient 查询结果 map 转为 TaskRow。
TaskRow TaskRowFromMap(const std::map<std::string, std::string> &row);

}  // namespace task
}  // namespace mmtrade
