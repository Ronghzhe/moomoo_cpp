#pragma once

// 任务延时埋点：各进程异步更新 staging JSON，Executor 终态时合并写入 CSV（一行一任务）。

#include <cstdint>
#include <string>
#include <vector>

namespace mmtrade {
namespace task {

class TaskLatencyRecorder {
 public:
  TaskLatencyRecorder();
  ~TaskLatencyRecorder();

  static TaskLatencyRecorder &Instance();

  void Configure(const std::string &staging_path, const std::string &csv_path);
  bool Enabled() const;

  void RecordTimestampMs(const char *field);
  void RecordValue(const char *field, int64_t value);
  void RecordString(const char *field, const std::string &value);
  void Increment(const char *field);

  void FlushAndWait();
  void WakeMerge();
  bool FinalizeCsvRow(int final_status, int exit_code);

  static int64_t WallMsNow();
  static int64_t WallUsNow();
  static std::string DefaultCsvPath();
  static std::string DefaultStagingDir();
  static std::string StagingPath(int64_t f_id, const std::string &task_name);
  static const std::vector<std::string> &CsvColumns();
  static void ConfigureFromEnv();

 private:
  TaskLatencyRecorder(const TaskLatencyRecorder &) = delete;
  TaskLatencyRecorder &operator=(const TaskLatencyRecorder &) = delete;

  struct Impl;
  Impl *impl_;
};

}  // namespace task
}  // namespace mmtrade
