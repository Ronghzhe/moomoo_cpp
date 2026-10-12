#include "task/task_repository.hpp"

#include "task/trade_executor.hpp"

#include <map>
#include <sstream>

namespace mmtrade {
namespace task {

TaskRepository::TaskRepository(db::MysqlClient *db) : db_(db) {}

// MySQL GET_LOCK(key, 0)：非阻塞抢锁，got=1 表示成功。
bool TaskRepository::TryGetLock(const std::string &lock_key) {
  db::MysqlRows rows;
  const std::string sql = "SELECT GET_LOCK('" + db_->Escape(lock_key) + "', 0) AS got";
  if (!db_->Query(sql, &rows) || rows.empty()) {
    return false;
  }
  return rows[0]["got"] == "1";
}

void TaskRepository::ReleaseLock(const std::string &lock_key) {
  db::MysqlRows rows;
  (void)db_->Query("SELECT RELEASE_LOCK('" + db_->Escape(lock_key) + "')", &rows);
}

void TaskRepository::ReleasePrepareLock(const std::string &lock_key) { ReleaseLock(lock_key); }

// 构造 SQL：AND f_id NOT IN (...)，排除 Executor 本进程已在跑的任务。
std::string TaskRepository::BuildExcludeIdsClause(const std::set<int64_t> &exclude_ids) const {
  if (exclude_ids.empty()) {
    return std::string();
  }
  std::ostringstream oss;
  oss << " AND f_id NOT IN (";
  bool first = true;
  for (std::set<int64_t>::const_iterator it = exclude_ids.begin(); it != exclude_ids.end(); ++it) {
    if (!first) {
      oss << ", ";
    }
    first = false;
    oss << *it;
  }
  oss << ")";
  return oss.str();
}

PrepareClaim TaskRepository::TryAcquirePrepareSlot(const std::string &worker_name, int task_type) {
  PrepareClaim out;
  if (db_ == NULL || !db_->IsConnected()) {
    return out;
  }

  // 1) 按 f_id 升序取一条 pending，保证 FIFO。
  db::MysqlRows rows;
  std::ostringstream sel;
  sel << "SELECT f_id, f_task_name, f_task_type, f_user, f_trader, f_task_params, f_updatetime "
         "FROM strategy_tasks "
         "WHERE f_status = " << kStatusPending << " AND f_task_type = " << task_type
      << " ORDER BY f_id ASC LIMIT 1";
  if (!db_->Query(sel.str(), &rows) || rows.empty()) {
    return out;
  }

  out.task = TaskRowFromMap(rows[0]);
  if (out.task.f_id <= 0) {
    return PrepareClaim();
  }

  // 2) 多 Worker 实例时，同一 f_id 只允许一个持有 prepare 锁。
  out.lock_key = PrepareLockKey(out.task.f_id);
  if (!TryGetLock(out.lock_key)) {
    return PrepareClaim();
  }

  // 3) 双检：抢锁期间可能被其他流程改状态。
  if (!FetchTaskStatus(out.task.f_id, &out.task.f_status) || out.task.f_status != kStatusPending) {
    ReleaseLock(out.lock_key);
    return PrepareClaim();
  }

  (void)InsertTaskLog(out.task.f_task_type, out.task.f_task_name, worker_name, out.task.f_trader,
                      out.task.f_task_params, "prepare_hold:" + worker_name, kStatusPending);
  // event_type=2: prepare_started
  (void)InsertTaskEvent(out.task.f_task_name, 2, "prepare_started");

  out.acquired = true;
  return out;
}

bool TaskRepository::FinalizePrepare(const TaskRow &task, bool prepared_ok, const std::string &result_json,
                                     const std::string &updated_task_params_json) {
  if (db_ == NULL || !db_->IsConnected() || task.f_id <= 0) {
    return false;
  }

  const int new_status = prepared_ok ? kStatusReady : kStatusFailed;
  // 乐观锁：仅当仍为 pending(1) 时更新，避免重复 finalize。
  std::ostringstream upd;
  upd << "UPDATE strategy_tasks SET f_status = " << new_status << ", f_task_result = '"
      << db_->Escape(result_json) << "' WHERE f_id = " << task.f_id << " AND f_status = " << kStatusPending;
  if (!db_->Execute(upd.str())) {
    return false;
  }
  if (db_->AffectedRows() != 1) {
    // event_type=9: 行状态与预期不符（并发或重复提交）
    (void)InsertTaskEvent(task.f_task_name, 9,
                          "finalize_prepare_row_mismatch expected_status=1 rowcount=" +
                              std::to_string(static_cast<long long>(db_->AffectedRows())));
    return false;
  }

  if (prepared_ok && !updated_task_params_json.empty()) {
    std::ostringstream params_upd;
    params_upd << "UPDATE strategy_tasks SET f_task_params = '" << db_->Escape(updated_task_params_json)
               << "' WHERE f_id = " << task.f_id;
    if (!db_->Execute(params_upd.str())) {
      return false;
    }
  }
  return true;
}

ExecutorClaim TaskRepository::ClaimExecutorTask(const std::string &executor_name, int task_type,
                                                int claim_status,
                                                const std::set<int64_t> &exclude_ids) {
  std::map<std::string, int> empty_limits;
  std::map<std::string, int> empty_inflight;
  std::set<std::string> empty_syms;
  return ClaimExecutorTask(executor_name, task_type, claim_status, exclude_ids, empty_limits,
                           empty_inflight, empty_syms, NULL);
}

ExecutorClaim TaskRepository::ClaimExecutorTask(const std::string &executor_name, int task_type,
                                                int claim_status,
                                                const std::set<int64_t> &exclude_ids,
                                                const std::map<std::string, int> &market_limits,
                                                const std::map<std::string, int> &market_in_flight,
                                                const std::set<std::string> &busy_symbols,
                                                SystemLogger *log) {
  ExecutorClaim out;
  if (db_ == NULL || !db_->IsConnected()) {
    return out;
  }

  // 一次拉最多 100 条候选，跳过 exclude_ids 后按市场/标的过滤取第一条（FIFO）。
  db::MysqlRows rows;
  std::ostringstream sel;
  sel << "SELECT f_id, f_task_name, f_task_type, f_user, f_trader, f_task_params, f_updatetime "
         "FROM strategy_tasks "
         "WHERE f_status = " << claim_status << " AND f_task_type = " << task_type
      << BuildExcludeIdsClause(exclude_ids) << " ORDER BY f_id ASC LIMIT 100";
  if (!db_->Query(sel.str(), &rows) || rows.empty()) {
    return out;
  }

  for (size_t i = 0; i < rows.size(); ++i) {
    out.task = TaskRowFromMap(rows[i]);
    if (out.task.f_id <= 0) {
      continue;
    }
    const std::string market = InferTaskMarket(out.task);
    if (!market_limits.empty()) {
      std::map<std::string, int>::const_iterator lit = market_limits.find(market);
      if (lit != market_limits.end()) {
        int used = 0;
        std::map<std::string, int>::const_iterator uit = market_in_flight.find(market);
        if (uit != market_in_flight.end()) {
          used = uit->second;
        }
        if (used >= lit->second) {
          continue;
        }
      }
    }
    const std::string symbol = InferTaskSymbol(out.task);
    if (!busy_symbols.empty() && !symbol.empty() &&
        busy_symbols.find(symbol) != busy_symbols.end()) {
      if (log != NULL) {
        log->Info("executor_symbol_busy_skip f_id=" +
                  std::to_string(static_cast<long long>(out.task.f_id)) +
                  " task=" + out.task.f_task_name + " symbol=" + symbol);
      }
      continue;
    }

    out.task.f_status = claim_status;
    (void)InsertTaskLog(out.task.f_task_type, out.task.f_task_name, executor_name, out.task.f_trader,
                        out.task.f_task_params, "executor_claim_by:" + executor_name, claim_status);
    (void)InsertTaskEvent(out.task.f_task_name, 5,
                          "executor_started_status_" + std::to_string(claim_status));
    if (log != NULL && !market_limits.empty()) {
      log->Info("executor_claim_market f_id=" +
                std::to_string(static_cast<long long>(out.task.f_id)) +
                " task=" + out.task.f_task_name + " market=" + market);
    }
    out.acquired = true;
    return out;
  }
  return ExecutorClaim();
}

bool TaskRepository::FinalizeExecutor(const TaskRow &task, int claim_status, int new_status,
                                      const std::string &result_json) {
  if (db_ == NULL || !db_->IsConnected() || task.f_id <= 0) {
    return false;
  }
  std::ostringstream upd;
  upd << "UPDATE strategy_tasks SET f_status = " << new_status << ", f_task_result = '"
      << db_->Escape(result_json) << "' WHERE f_id = " << task.f_id << " AND f_status = " << claim_status;
  if (!db_->Execute(upd.str())) {
    return false;
  }
  return db_->AffectedRows() == 1;
}

bool TaskRepository::InsertTaskLog(int task_type, const std::string &task_name, const std::string &user,
                                   const std::string &trader, const std::string &trade_params,
                                   const std::string &trade_result, int status) {
  if (db_ == NULL || !db_->IsConnected()) {
    return false;
  }
  std::ostringstream sql;
  sql << "INSERT INTO strategy_logs "
         "(f_uuid, f_task_type, f_task_name, f_user, f_trader, f_task_params, f_trade_result, f_status) "
         "VALUES (0, "
      << task_type << ", '" << db_->Escape(task_name) << "', '" << db_->Escape(user) << "', '"
      << db_->Escape(trader) << "', '" << db_->Escape(trade_params) << "', '" << db_->Escape(trade_result)
      << "', " << status << ")";
  return db_->Execute(sql.str());
}

bool TaskRepository::InsertTaskEvent(const std::string &task_name, int event_type,
                                     const std::string &event_msg) {
  if (db_ == NULL || !db_->IsConnected()) {
    return false;
  }
  std::ostringstream sql;
  sql << "INSERT INTO strategy_task_events (f_uuid, f_task_name, f_event_type, f_event_msg) VALUES (0, '"
      << db_->Escape(task_name) << "', " << event_type << ", '" << db_->Escape(event_msg) << "')";
  return db_->Execute(sql.str());
}

bool TaskRepository::FetchTaskStatus(int64_t f_id, int *status_out) {
  if (status_out == NULL || db_ == NULL || !db_->IsConnected() || f_id <= 0) {
    return false;
  }
  db::MysqlRows rows;
  std::ostringstream sql;
  sql << "SELECT f_status FROM strategy_tasks WHERE f_id = " << f_id << " LIMIT 1";
  if (!db_->Query(sql.str(), &rows) || rows.empty()) {
    return false;
  }
  *status_out = atoi(rows[0]["f_status"].c_str());
  return true;
}

}  // namespace task
}  // namespace mmtrade
