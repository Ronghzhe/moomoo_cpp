#pragma once

// Worker prepare 成功后：读 mapped config，向 Telegram 发送「已入队 f_status=2」监控摘要。
// Executor 路径下 strategy_cpp 会跳过重复的 startup TG（startup_notify_once=false）。

#include <string>

namespace mmtrade {
namespace task {

bool NotifyTaskQueueReady(const std::string &mapped_path, int64_t f_id, const std::string &task_name,
                          std::string *detail_out);

}  // namespace task
}  // namespace mmtrade
