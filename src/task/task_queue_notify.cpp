#include "task/task_queue_notify.hpp"

#include "config/config_persist.hpp"
#include "config/strategy_loader.hpp"
#include "log/strategy_message_format.hpp"
#include "notify/strategy_notify.hpp"

namespace mmtrade {
namespace task {

// 从 mapped JSON 加载策略配置，格式化监控启动消息并 SendStrategyNotifyText。
bool NotifyTaskQueueReady(const std::string &mapped_path, int64_t f_id, const std::string &task_name,
                          std::string *detail_out) {
  (void)f_id;
  try {
    const mmtrade::config::StrategyConfig cfg = mmtrade::config::LoadConfig(mapped_path);
    if (cfg.symbols.empty()) {
      if (detail_out != NULL) {
        *detail_out = "no symbol in mapped config";
      }
      return false;
    }
    const mmtrade::config::SymbolConfig &sym = cfg.symbols[0];
    if (!mmtrade::notify::TelegramStrategyEnabled(cfg)) {
      if (detail_out != NULL) {
        *detail_out = "disabled";
      }
      return true;
    }
    const std::string trd_env = mmtrade::notify::ResolveTrdEnvLabel(cfg);
    std::string body = mmtrade::log::FormatMonitorStartupMessage(cfg, sym, trd_env);
    body += "\n队列: 已入队 f_status=2 (C++ worker), 等待 Executor 执行";
    const mmtrade::notify::NotifyResult nr = mmtrade::notify::SendStrategyNotifyText(cfg, body);
    if (!nr.ok) {
      if (detail_out != NULL) {
        *detail_out = nr.detail;
      }
      return false;
    }
    try {
      mmtrade::config::PersistStartupNotifyOnceConsumed(mapped_path, sym.code);
    } catch (...) {
    }
    if (detail_out != NULL) {
      detail_out->clear();
    }
    return true;
  } catch (const std::exception &e) {
    if (detail_out != NULL) {
      *detail_out = e.what();
    }
    return false;
  }
}

}  // namespace task
}  // namespace mmtrade
