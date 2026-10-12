#pragma once

// task_worker — Worker 阶段的业务逻辑（不含轮询循环，循环在 task_worker_main）。
//
// PrepareTask：校验 f_task_params → 查账户/模式 → 映射 strategy JSON → 写 backup 文件。
// FinalizePreparedTask：写 DB f_status=2、审计日志、可选 Telegram「已入队」通知。
//
// strategy_task_events 常用 event_type（FinalizePreparedTask 内）：
//   3 prepared_ready_status_2  4 prepare_failed  7 telegram 成功  8 telegram 失败

#include "db/mysql_client.hpp"
#include "task/system_log.hpp"
#include "task/task_repository.hpp"

#include <nlohmann/json.hpp>
#include <string>

namespace mmtrade {
namespace task {

struct PrepareTaskResult {
  bool ok;
  nlohmann::json result;  // 写入 f_task_result 的 Worker 侧摘要 JSON
};

// 对单条 pending 任务做 prepare（不访问 strategy_tasks 状态，不写 DB）。
// force_simulate=true 时强制 paper 模式（worker --simulate）。
PrepareTaskResult PrepareTask(const TaskRow &task, db::MysqlClient *db, bool force_simulate,
                              SystemLogger *log);

// prepare 完成后更新 DB、写 strategy_logs/events，成功时调用 NotifyTaskQueueReady。
bool FinalizePreparedTask(TaskRepository *repo, const TaskRow &task, bool prepared_ok,
                          const nlohmann::json &result, const std::string &worker_name,
                          SystemLogger *log);

}  // namespace task
}  // namespace mmtrade
