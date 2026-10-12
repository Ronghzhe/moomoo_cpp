#include "task/task_worker.hpp"

// Worker prepare 实现：不启动 strategy，只生成 mapped backup 供 Executor 使用。

#include "task/account_env.hpp"
#include "task/task_backup_store.hpp"
#include "task/task_mapper.hpp"
#include "task/task_params_spec.hpp"
#include "task/task_paths.hpp"
#include "task/task_queue_notify.hpp"
#include "task/task_result.hpp"

#include <ctime>

namespace mmtrade {
namespace task {

namespace {

std::string NowIso() {
  const std::time_t now = std::time(NULL);
  std::tm tm_buf;
  localtime_r(&now, &tm_buf);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);
  return std::string(buf);
}

}  // namespace

// 步骤：parse/validate → 账户 → paper|real → BuildStrategyConfig → WriteTaskPairBackups。
PrepareTaskResult PrepareTask(const TaskRow &task, db::MysqlClient *db, bool force_simulate,
                              SystemLogger *log) {
  PrepareTaskResult out;
  out.ok = false;
  const std::string task_name = task.f_task_name;
  const std::string f_user = task.f_user;
  const std::string start_iso = NowIso();
  std::string account_code;
  std::string mode_str = "paper";
  bool paper_mode = true;

  try {
    nlohmann::json payload = nlohmann::json::parse(task.f_task_params);
    ValidateTaskParams(payload);
    account_code = ResolveAccountCode(f_user, payload, task_name, db);
    const std::pair<bool, std::string> mode = ResolvePaperMode(account_code, db, force_simulate);
    paper_mode = mode.first;
    mode_str = mode.second;

    const MappedConfigResult mapped = BuildStrategyConfig(task_name, payload, paper_mode);
    const BackupWriteResult backup = WriteTaskPairBackups(DefaultBackupDir(), f_user, task_name,
                                                          payload, mapped.config);

    if (log != NULL) {
      log->Info("task_prepared f_id=" + std::to_string(static_cast<long long>(task.f_id)) +
                " account=" + account_code + " mode=" + mode_str + " raw=" + backup.raw_path +
                " mapped=" + backup.mapped_path);
    }

    out.ok = true;
    out.result = nlohmann::json{{"task_name", task_name},
                                {"start_time", start_iso},
                                {"finish_time", NowIso()},
                                {"ok", true},
                                {"account", account_code},
                                {"mode", mode_str},
                                {"simulate", paper_mode},
                                {"mapped_summary", mapped.summary},
                                {"f_user", f_user},
                                {"raw_backup", backup.raw_path},
                                {"mapped_backup", backup.mapped_path},
                                {"task_params", payload},
                                {"note", "task prepared; ready for independent C++ executor"}};
    return out;
  } catch (const AccountConfigError &ace) {
    if (log != NULL) {
      log->Error("task_prepare_account_error name=" + task_name + " err=" + ace.what());
    }
    const nlohmann::json err_obj = ErrorForCode("ACCOUNT_ERROR", ace.what());
    out.result = nlohmann::json{{"task_name", task_name},
                                {"start_time", start_iso},
                                {"finish_time", NowIso()},
                                {"ok", false},
                                {"account", account_code},
                                {"mode", mode_str},
                                {"simulate", false},
                                {"error", ace.what()},
                                {"note", "account configuration error"},
                                {"f_user", f_user},
                                {"f_task_result", err_obj}};
    return out;
  } catch (const std::exception &e) {
    if (log != NULL) {
      log->Error(std::string("task_prepare_failed name=") + task_name + " err=" + e.what());
    }
    out.result = nlohmann::json{{"task_name", task_name},
                                {"start_time", start_iso},
                                {"finish_time", NowIso()},
                                {"ok", false},
                                {"account", account_code},
                                {"mode", mode_str},
                                {"simulate", paper_mode},
                                {"error", e.what()},
                                {"note", "task param validation/mapping/backup failed"},
                                {"f_user", f_user}};
    return out;
  }
}

// 调用 repo->FinalizePrepare；成功时 event 3 + 可选 TG 入队通知（event 7/8）。
bool FinalizePreparedTask(TaskRepository *repo, const TaskRow &task, bool prepared_ok,
                          const nlohmann::json &result, const std::string &worker_name,
                          SystemLogger *log) {
  if (repo == NULL) {
    return false;
  }
  const nlohmann::json f_task_public = ComposeFTaskResultFromWorker(result);
  const std::string result_json = f_task_public.dump();
  std::string updated_params;
  if (prepared_ok && result.contains("task_params") && result.at("task_params").is_object()) {
    updated_params = result.at("task_params").dump();
  }
  const bool updated = repo->FinalizePrepare(task, prepared_ok, result_json, updated_params);
  if (prepared_ok && updated) {
    repo->InsertTaskLog(task.f_task_type, task.f_task_name, worker_name, task.f_trader,
                        task.f_task_params,
                        "prepared_ready_status_2 mapped=" + result.value("mapped_backup", ""), kStatusReady);
    repo->InsertTaskEvent(task.f_task_name, 3, "prepared_ready_status_2");
    const std::string mapped_backup = result.value("mapped_backup", "");
    if (!mapped_backup.empty()) {
      std::string notify_detail;
      const bool n_ok = NotifyTaskQueueReady(mapped_backup, task.f_id, task.f_task_name, &notify_detail);
      if (n_ok) {
        repo->InsertTaskEvent(task.f_task_name, 7, "telegram_queue_ready_sent");
      } else {
        repo->InsertTaskEvent(task.f_task_name, 8, "telegram_queue_ready_failed:" + notify_detail.substr(0, 400));
        if (log != NULL) {
          log->Warn("telegram_queue_ready_failed task=" + task.f_task_name + " err=" + notify_detail);
        }
      }
    }
  } else if (!prepared_ok && updated) {
    const std::string fail_snip = result.value("error", "").substr(0, 500);
    repo->InsertTaskLog(task.f_task_type, task.f_task_name, worker_name, task.f_trader, task.f_task_params,
                        "prepare_fail:" + fail_snip, kStatusFailed);
    repo->InsertTaskEvent(task.f_task_name, 4, "prepare_failed");
  }
  return updated;
}

}  // namespace task
}  // namespace mmtrade
