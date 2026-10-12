#pragma once

// TaskRepository — 任务队列与 MySQL strategy_tasks 的读写层。
//
// 职责划分：
//   Worker 路径：TryAcquirePrepareSlot → PrepareTask（见 task_worker）→ FinalizePrepare
//   Executor 路径：ClaimExecutorTask → RunOneTask（见 trade_executor）→ FinalizeExecutor
//
// 审计：InsertTaskLog / InsertTaskEvent 写入 strategy_logs、strategy_task_events。

#include "db/mysql_client.hpp"
#include "task/task_types.hpp"
#include "task/system_log.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace mmtrade {
namespace task {

class TaskRepository {
 public:
  explicit TaskRepository(db::MysqlClient *db);

  // Worker 轮询入口：取最早一条 f_status=1 且匹配 f_task_type 的任务，
  // 用 GET_LOCK(stp_<f_id>) 防并发；成功则写 prepare_started 事件。
  // 调用方必须在 prepare 完成后调用 ReleasePrepareLock。
  PrepareClaim TryAcquirePrepareSlot(const std::string &worker_name, int task_type);

  // 释放 TryAcquirePrepareSlot 持有的 MySQL 锁。
  void ReleasePrepareLock(const std::string &lock_key);

  // Worker prepare 收尾：f_status 1→2（成功）或 1→0（失败），写入 f_task_result。
  // prepared_ok 且 updated_task_params_json 非空时，会刷新 f_task_params。
  bool FinalizePrepare(const TaskRow &task, bool prepared_ok, const std::string &result_json,
                       const std::string &updated_task_params_json);

  // Executor 轮询入口：取 f_status=claim_status 的任务（默认 2），
  // 跳过 exclude_ids（本进程正在执行 / 冷却中的 f_id）；写 executor_started 事件。
  // 可选：按市场配额 / 同标的互斥过滤候选（对齐 Python trade_executor）。
  // 注意：不做行级 UPDATE 占位，靠 exclude_ids 避免同进程重复认领。
  ExecutorClaim ClaimExecutorTask(const std::string &executor_name, int task_type, int claim_status,
                                  const std::set<int64_t> &exclude_ids);

  ExecutorClaim ClaimExecutorTask(const std::string &executor_name, int task_type, int claim_status,
                                  const std::set<int64_t> &exclude_ids,
                                  const std::map<std::string, int> &market_limits,
                                  const std::map<std::string, int> &market_in_flight,
                                  const std::set<std::string> &busy_symbols, SystemLogger *log);

  // Executor 收尾：f_status claim_status→new_status（3=done / 0=failed / 保持 2），
  // 条件 UPDATE 防止并发覆盖。
  bool FinalizeExecutor(const TaskRow &task, int claim_status, int new_status,
                        const std::string &result_json);

  // 写入 strategy_logs（人工可读流水，如 prepare_hold、executor_claim_by）。
  bool InsertTaskLog(int task_type, const std::string &task_name, const std::string &user,
                     const std::string &trader, const std::string &trade_params,
                     const std::string &trade_result, int status);

  // 写入 strategy_task_events（结构化事件，event_type 见 task_worker / trade_executor 注释）。
  bool InsertTaskEvent(const std::string &task_name, int event_type, const std::string &event_msg);

  // 按 f_id 读取当前 f_status（抢锁后二次确认 status 仍为 pending 时用）。
  bool FetchTaskStatus(int64_t f_id, int *status_out);

 private:
  db::MysqlClient *db_;

  bool TryGetLock(const std::string &lock_key);
  void ReleaseLock(const std::string &lock_key);
  std::string BuildExcludeIdsClause(const std::set<int64_t> &exclude_ids) const;
};

}  // namespace task
}  // namespace mmtrade
