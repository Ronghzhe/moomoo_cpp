#pragma once

// trade_executor — Executor 阶段的业务逻辑（不含轮询循环，循环在 trade_executor_main）。
//
// RunOneTask：等 mapped backup → shell 执行 run_strategy.sh -c mapped.json → 读结果。
// FinalizeExecutorTask：根据 ok / task_completed 决定 f_status 3|0|保持 claim_status。
//
// strategy_task_events 常用 event_type（FinalizeExecutorTask 内）：
//   6 executor_finished_*  9 finalize 行状态不匹配

#include "db/mysql_client.hpp"
#include "task/system_log.hpp"
#include "task/strategy_worker_pool.hpp"
#include "task/task_repository.hpp"

#include <nlohmann/json.hpp>
#include <set>
#include <string>

namespace mmtrade {
namespace task {

struct ExecutorRunResult {
  bool ok;                 // 进程 exit 0 且非 trading_fatal
  nlohmann::json payload;  // 写入 f_task_result 的 Executor 侧摘要 JSON（含 duration_sec）
};

// 从 f_task_params 推断市场 / 标的（对齐 Python trade_executor）。
std::string InferTaskMarket(const TaskRow &task);
std::string InferTaskSymbol(const TaskRow &task);
bool EntryNetTestEnabled();

// 执行单条已认领任务：open_stagger 错峰 → 等 mapped 文件 → strategy pool 或 system(run_strategy.sh)。
// timeout_sec>0 时用 timeout 包裹 shell；open_stagger 仅在美东/上海工作日 09:25–09:40
// 按 f_id 分散同时开仓 burst（对齐 Python trade_executor）。
// pool 非空时优先派发到常驻 strategy_cpp worker（复用 InitQot/InitTrd）。
ExecutorRunResult RunOneTask(const TaskRow &task, int timeout_sec, SystemLogger *log,
                             double open_stagger_slot_sec, int open_stagger_buckets,
                             StrategyWorkerPool *pool);

// 写 DB 终态并记 strategy_logs/events。
// 返回值：>0 表示需要对本 f_id 做冷却（秒），如 unlock rate-limit requeue。
double FinalizeExecutorTask(TaskRepository *repo, const TaskRow &task, int claim_status,
                            const nlohmann::json &result, bool ok, const std::string &executor_name);

}  // namespace task
}  // namespace mmtrade
