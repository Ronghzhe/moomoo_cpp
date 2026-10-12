#pragma once

#include "strategy/net_split_leg_wait.hpp"
#include "strategy/symbol_loop_state.hpp"
#include "strategy/tick_context.hpp"

namespace mmtrade {
namespace config {
struct StrategyConfig;
}
namespace log {
class StrategyStore;
}
namespace opend {
class OrderClient;
}
namespace strategy {

struct EntryNetSplitParams {
  SymbolLoopState *state;
  log::StrategyStore *store;
  opend::OrderClient *client;
  const config::StrategyConfig *cfg;
  std::string cfg_path;
  std::string trd_env;
  const BookTick *book;
  double px;
  double qty;
  std::string remark;
  bool book_guard_emergency;
  bool in_rth;
};

bool TryExecuteEntryNetSplit(const EntryNetSplitParams &params);

struct ExitNetSplitParams {
  SymbolLoopState *state;
  log::StrategyStore *store;
  opend::OrderClient *client;
  const config::StrategyConfig *cfg;
  std::string cfg_path;
  std::string trd_env;
  const BookTick *book;
  double px;
  double exit_slice;
  std::string remark;
  std::string exit_sz_note;
  std::string exit_rule_id;
  std::string fill_mode;
  bool in_rth;
  bool book_guard_emergency;
};

bool TryExecuteExitNetSplit(const ExitNetSplitParams &params);

}  // namespace strategy
}  // namespace mmtrade
