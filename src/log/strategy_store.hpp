#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mmtrade {
namespace log {

class StrategyStore {
 public:
  StrategyStore(const std::string &csv_dir, const std::string &symbol, std::int64_t acc_id,
                int acc_index);

  const std::string &csv_dir_path() const { return csv_dir_path_; }
  const std::string &runtime_log_path() const { return runtime_file_; }

  void LogEvent(const std::string &level, const std::string &event_type,
                const std::string &message);

  // Direct runtime.log line(s) without event_type prefix (aligned with Python setup_runtime_logger).
  void LogRuntimeLine(const std::string &level, const std::string &message);
  void LogRuntimeBlock(const std::string &level, const std::string &multiline_text);

  void SaveOrder(const std::string &task_id, const std::string &symbol, const std::string &side,
                 const std::string &order_id, const std::string &status, double qty, double price,
                 const std::string &remark, const std::string &budget = "",
                 const std::string &est_notional = "", const std::string &sizing = "",
                 const std::string &trd_env = "", const std::string &profit_status = "",
                 const std::string &profit_pct = "", const std::string &profit_amount = "");

  bool UpdateOrderFill(const std::string &order_id, const std::string &status, double price,
                       double qty);

  bool UpdateOrderProfit(const std::string &order_id, const std::string &trd_env,
                         const std::string &profit_status, const std::string &profit_pct,
                         const std::string &profit_amount);

 private:
  void EnsureHeaders();
  bool LoadOrderRows(std::vector<std::string> *header, std::vector<std::vector<std::string> > *rows);
  bool WriteOrderRows(const std::vector<std::string> &header,
                      const std::vector<std::vector<std::string> > &rows);
  int FindLastOrderRow(const std::vector<std::string> &header,
                       const std::vector<std::vector<std::string> > &rows,
                       const std::string &order_id) const;

  std::string symbol_;
  std::string csv_dir_path_;
  std::string events_file_;
  std::string orders_file_;
  std::string runtime_file_;
  std::string runtime_logger_name_;

  void LogRuntime(const std::string &level, const std::string &message);
};

}  // namespace log
}  // namespace mmtrade
