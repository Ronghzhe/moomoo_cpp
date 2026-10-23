#include "config/config_path_resolver.hpp"
#include "config/config_persist.hpp"
#include "config/strategy_loader.hpp"
#include "log/strategy_console_log.hpp"
#include "log/strategy_message_format.hpp"
#include "log/strategy_runtime_log.hpp"
#include "log/strategy_store.hpp"
#include "notify/strategy_notify.hpp"
#include "opend/order_client.hpp"
#include "opend/broker_factory.hpp"
#include "opend/quote_connection.hpp"
#include "task/pipeline_latency.hpp"
#include "task/task_latency_recorder.hpp"
#include "strategy/live_eval_compute.hpp"
#include "strategy/live_eval_inject.hpp"
#include "strategy/order_executor.hpp"
#include "strategy/order_pending_loop.hpp"
#include "strategy/order_session_policy.hpp"
#include "strategy/position_sizing.hpp"
#include "strategy/strategy_runner.hpp"
#include "strategy/symbol_loop_state.hpp"
#include "strategy/symbol_tradability.hpp"
#include "strategy/tick_builder.hpp"

#include "Include_MMAPI.h"

#include <google/protobuf/stubs/common.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

volatile sig_atomic_t g_run = 1;
volatile sig_atomic_t g_stop_worker = 0;

// Mirrors Python EXIT_CODE_TRADING_FATAL so executor finalizes fail and moves on.
constexpr int kExitTradingFatal = 2;

std::string g_session_error;

struct PoolTradeReuse {
  bool valid;
  std::uint64_t acc_id;
  bool simulate;

  PoolTradeReuse() : valid(false), acc_id(0), simulate(true) {}
};

PoolTradeReuse g_pool_trade;

struct TradingFatalExit {
  bool active;
  std::string symbol;
  std::string code;
  std::string detail;

  TradingFatalExit() : active(false) {}
};

TradingFatalExit g_fatal_exit;

void OnSignal(int) {
  g_run = 0;
  g_stop_worker = 1;
}

void WriteRunOutcome(const std::string &config_path, const TradingFatalExit &fatal) {
  if (config_path.empty() || !fatal.active) {
    return;
  }
  try {
    const size_t slash = config_path.find_last_of("/\\");
    const std::string dir = (slash == std::string::npos) ? "." : config_path.substr(0, slash);
    nlohmann::json j = {{"fatal", true},
                        {"code", fatal.code.empty() ? "TRADING_FATAL" : fatal.code},
                        {"symbol", fatal.symbol},
                        {"detail", fatal.detail}};
    std::ofstream ofs((dir + "/run_outcome.json").c_str());
    ofs << j.dump();
  } catch (...) {
  }
}

void RequestTradingFatalStop(const mmtrade::strategy::SymbolLoopState &st, const char *via) {
  if (st.trading_fatal_code.empty() && !st.entry_blocked) {
    return;
  }
  g_fatal_exit.active = true;
  g_fatal_exit.symbol = st.sym.code;
  g_fatal_exit.code =
      st.trading_fatal_code.empty() ? std::string("TRADING_FATAL") : st.trading_fatal_code;
  g_fatal_exit.detail = st.trading_fatal_detail;
  std::cerr << "trading_fatal via=" << (via != NULL ? via : "?") << " code=" << st.sym.code
            << " fatal_code=" << g_fatal_exit.code << " detail=" << g_fatal_exit.detail << "\n";
  mmtrade::log::LogConsoleWarn(
      std::string("trading_fatal stop symbol=") + st.sym.code + " code=" + g_fatal_exit.code +
      " detail=" + g_fatal_exit.detail);
  g_run = 0;
}

double NowUnix() {
  return static_cast<double>(std::time(NULL));
}

double NowMono() {
  const std::chrono::steady_clock::time_point tp = std::chrono::steady_clock::now();
  return std::chrono::duration<double>(tp.time_since_epoch()).count();
}

std::string JsonEscape(const std::string &s) {
  std::ostringstream oss;
  for (size_t i = 0; i < s.size(); ++i) {
    const char c = s[i];
    if (c == '"') {
      oss << "\\\"";
    } else if (c == '\\') {
      oss << "\\\\";
    } else {
      oss << c;
    }
  }
  return oss.str();
}

// 主循环 WaitBookPush 默认/下限：有 push 立即唤醒，无 push 时最多等待该秒数再跑一轮 judge/追单。
const double kBookPollSec = 0.5;

struct AppArgs {
  std::string config_path;
  std::string host;
  int port;
  double poll_sec;
  int max_ticks;
  bool dry_run;
  bool verbose_json;

  AppArgs() : port(0), poll_sec(kBookPollSec), max_ticks(0), dry_run(false), verbose_json(false) {}
};

bool ParseAppFlags(int argc, char **argv, AppArgs *out) {
  if (out == NULL) {
    return false;
  }
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "-hk" || a == "--hk" || a == "-us" || a == "--us") {
      continue;
    }
    if ((a == "-c" || a == "--config") && i + 1 < argc) {
      ++i;
      continue;
    }
    if (!a.empty() && a[0] != '-' && out->config_path.empty()) {
      continue;
    }
    if (a == "--host" && i + 1 < argc) {
      out->host = argv[++i];
    } else if (a == "--port" && i + 1 < argc) {
      out->port = std::atoi(argv[++i]);
    } else if (a == "--poll-sec" && i + 1 < argc) {
      out->poll_sec = std::atof(argv[++i]);
    } else if (a == "--max-ticks" && i + 1 < argc) {
      out->max_ticks = std::atoi(argv[++i]);
    } else if (a == "--dry-run" || a == "--judge-only") {
      out->dry_run = true;
    } else if (a == "--place-orders") {
      out->dry_run = false;
    } else if (a == "--verbose-json") {
      out->verbose_json = true;
    }
  }
  return true;
}

mmtrade::strategy::BookTick ToBookTick(const mmtrade::opend::QuoteSnapshot &snap) {
  mmtrade::strategy::BookTick book;
  book.valid = snap.valid;
  book.bid_1 = snap.bid_1;
  book.ask_1 = snap.ask_1;
  return book;
}

std::string MarketTagFromSymbolCode(const std::string &code) {
  if (code.size() >= 3 && code.compare(0, 3, "HK.") == 0) {
    return "HK";
  }
  return "US";
}

void RefreshAndApplyMarketPhases(mmtrade::opend::QuoteConnection *opend,
                                 std::map<std::string, mmtrade::strategy::SymbolLoopState> *loops,
                                 double now, double *last_poll_ts) {
  if (opend == NULL || loops == NULL || last_poll_ts == NULL || loops->empty()) {
    return;
  }
  if (*last_poll_ts > 0.0 && (now - *last_poll_ts) < 30.0) {
    return;
  }
  std::map<std::string, std::string> sample_by_tag;
  for (std::map<std::string, mmtrade::strategy::SymbolLoopState>::const_iterator it =
           loops->begin();
       it != loops->end(); ++it) {
    const std::string tag = MarketTagFromSymbolCode(it->first);
    if (sample_by_tag.find(tag) == sample_by_tag.end()) {
      sample_by_tag[tag] = it->first;
    }
  }
  std::vector<std::string> sample_codes;
  for (std::map<std::string, std::string>::const_iterator it = sample_by_tag.begin();
       it != sample_by_tag.end(); ++it) {
    sample_codes.push_back(it->second);
  }
  *last_poll_ts = now;
  opend->RefreshMarketState(sample_codes);
  for (std::map<std::string, mmtrade::strategy::SymbolLoopState>::iterator it = loops->begin();
       it != loops->end(); ++it) {
    const std::string phase = opend->MarketPhaseForCode(it->first);
    if (!phase.empty()) {
      it->second.last_market_phase = phase;
    }
  }
}

bool ShouldAutoStopAfterClose(
    const mmtrade::config::StrategyConfig &cfg,
    const std::map<std::string, mmtrade::strategy::SymbolLoopState> &loops,
    mmtrade::opend::QuoteConnection *opend) {
  if (!cfg.auto_stop_after_close || opend == NULL || loops.empty()) {
    return false;
  }
  bool any_rth_session = false;
  std::set<std::string> tags;
  for (std::map<std::string, mmtrade::strategy::SymbolLoopState>::const_iterator it =
           loops.begin();
       it != loops.end(); ++it) {
    const std::string sess =
        it->second.sym.order_session.empty() ? "RTH" : it->second.sym.order_session;
    if (mmtrade::strategy::EffectiveAutoStopAfterClose(true, sess)) {
      any_rth_session = true;
    }
    tags.insert(MarketTagFromSymbolCode(it->first));
  }
  if (!any_rth_session) {
    return false;
  }
  for (std::set<std::string>::const_iterator it = tags.begin(); it != tags.end(); ++it) {
    if (opend->MarketPhaseForTag(*it) != "post") {
      return false;
    }
  }
  return true;
}

void InitLoopState(mmtrade::strategy::SymbolLoopState *st) {
  if (st == NULL) {
    return;
  }
  st->entry_done = st->sym.status >= 2;
  st->exit_done = st->sym.status >= 3;
  st->lot_size = mmtrade::strategy::DefaultLotSizeForCode(st->sym.code);
}

void RunStartupHooks(mmtrade::strategy::SymbolLoopState *st,
                     const mmtrade::config::StrategyConfig &cfg, const std::string &config_path,
                     const std::string &trd_env, mmtrade::log::StrategyStore *store) {
  if (st == NULL || store == NULL) {
    return;
  }
  const std::string startup_msg = "启动成功 symbol=" + st->sym.code + " mode=" + cfg.mode +
                                  " status=" + mmtrade::log::StatusLabel(st->sym.status) +
                                  " exit_enabled=" + mmtrade::log::BoolMark(st->sym.exit.enabled);
  store->LogEvent("INFO", "startup", startup_msg);
  mmtrade::log::LogConsoleInfo("[" + st->sym.code + "] " + startup_msg);

  // Executor 路径：Worker 已在入队时推送监控摘要并写回 startup_notify_once=false。
  // 此处仅保留交易类通知（成交/拒单等），避免重复发送【监控：平仓】。
  if (!st->sym.startup_notify_once) {
    store->LogEvent("INFO", "startup_notify_skipped",
                    "监控摘要已由 Worker 推送，Executor 跳过重复通知");
    mmtrade::log::LogConsoleInfo("[" + st->sym.code +
                                   "] 监控摘要已由 Worker 推送，跳过重复 Telegram");
    return;
  }

  mmtrade::log::LogMonitorStartupBlock(store, cfg, st->sym, trd_env);

  if (mmtrade::notify::TelegramStrategyEnabled(cfg)) {
    const std::string msg = mmtrade::log::FormatMonitorStartupMessage(cfg, st->sym, trd_env);
    const mmtrade::notify::NotifyResult nr = mmtrade::notify::SendStrategyNotifyText(cfg, msg);
    if (nr.ok) {
      store->LogEvent("INFO", "telegram_startup_sent", "启动监控摘要已推送 Telegram");
      mmtrade::log::LogConsoleInfo("[" + st->sym.code + "] Telegram 启动通知已发送");
    } else {
      store->LogEvent("WARN", "telegram_startup_failed", nr.detail);
      mmtrade::log::LogConsoleWarn("[" + st->sym.code + "] Telegram 启动通知失败: " + nr.detail);
    }
  } else {
    mmtrade::log::LogConsoleInfo("[" + st->sym.code + "] Telegram 通知未启用");
  }

  try {
    mmtrade::config::PersistStartupNotifyOnceConsumed(config_path, st->sym.code);
    st->sym.startup_notify_once = false;
  } catch (const std::exception &e) {
    store->LogEvent("WARN", "startup_notify_persist_failed", e.what());
    mmtrade::log::LogConsoleWarn(std::string("startup_notify_persist_failed: ") + e.what());
  }
}

void PersistRuntimeQuiet(const std::string &config_path, const mmtrade::config::SymbolConfig &sym,
                         mmtrade::log::StrategyStore *store) {
  try {
    mmtrade::config::PersistStrategyConfigRuntime(config_path, sym);
  } catch (const std::exception &e) {
    if (store != NULL) {
      store->LogEvent("WARN", "persist_runtime_failed", e.what());
    }
  }
}

bool AllManagedDone(
    const std::map<std::string, mmtrade::strategy::SymbolLoopState> &loops) {
  if (loops.empty()) {
    return true;
  }
  for (std::map<std::string, mmtrade::strategy::SymbolLoopState>::const_iterator it =
           loops.begin();
       it != loops.end(); ++it) {
    if (!mmtrade::strategy::SymbolManagedDone(it->second)) {
      return false;
    }
  }
  return true;
}

bool AnyAwaitingFill(const std::map<std::string, mmtrade::strategy::SymbolLoopState> &loops) {
  for (std::map<std::string, mmtrade::strategy::SymbolLoopState>::const_iterator it =
           loops.begin();
       it != loops.end(); ++it) {
    if (mmtrade::strategy::IsAwaitingFill(it->second)) {
      return true;
    }
  }
  return false;
}

std::string JoinLoopCodes(
    const std::map<std::string, mmtrade::strategy::SymbolLoopState> &loops) {
  std::ostringstream oss;
  bool first = true;
  for (std::map<std::string, mmtrade::strategy::SymbolLoopState>::const_iterator it =
           loops.begin();
       it != loops.end(); ++it) {
    if (!first) {
      oss << ",";
    }
    first = false;
    oss << it->first;
  }
  return oss.str();
}

void PersistAllRuntimeQuiet(
    const std::string &config_path,
    const std::map<std::string, mmtrade::strategy::SymbolLoopState> &loops,
    const std::map<std::string, std::unique_ptr<mmtrade::log::StrategyStore> > &stores) {
  for (std::map<std::string, mmtrade::strategy::SymbolLoopState>::const_iterator it =
           loops.begin();
       it != loops.end(); ++it) {
    mmtrade::log::StrategyStore *store = NULL;
    std::map<std::string, std::unique_ptr<mmtrade::log::StrategyStore> >::const_iterator sit =
        stores.find(it->first);
    if (sit != stores.end()) {
      store = sit->second.get();
    }
    PersistRuntimeQuiet(config_path, it->second.sym, store);
  }
}

int ExitIfManagedDoneAtStartup(
    const std::string &config_path,
    const std::map<std::string, mmtrade::strategy::SymbolLoopState> &loops,
    const std::map<std::string, std::unique_ptr<mmtrade::log::StrategyStore> > &stores) {
  if (!AllManagedDone(loops)) {
    return -1;
  }
  const std::string codes_label = JoinLoopCodes(loops);
  bool all_status_three = true;
  for (std::map<std::string, mmtrade::strategy::SymbolLoopState>::const_iterator it =
           loops.begin();
       it != loops.end(); ++it) {
    if (it->second.sym.status < 3) {
      all_status_three = false;
      break;
    }
  }
  if (all_status_three) {
    for (std::map<std::string, mmtrade::strategy::SymbolLoopState>::const_iterator it =
             loops.begin();
         it != loops.end(); ++it) {
      mmtrade::log::StrategyStore *store = NULL;
      std::map<std::string, std::unique_ptr<mmtrade::log::StrategyStore> >::const_iterator sit =
          stores.find(it->first);
      if (sit != stores.end()) {
        store = sit->second.get();
      }
      const std::string msg =
          "标的 status=3 本轮已结束；重跑请将该条 symbols 配置 status 置 0 / cycle_done | " +
          it->first;
      mmtrade::log::LogConsoleInfo("[" + it->first + "] " + msg);
      if (store != NULL) {
        store->LogEvent("INFO", "cycle_done_skip", it->first + " status=3 skip");
      }
    }
  } else {
    mmtrade::log::LogConsoleInfo(
        "跳过订阅与行情循环：全部标的已由引擎托管完毕（含 exit_enabled=false 且已开仓）| "
        "subscribe_skip_managed_done | " +
        codes_label);
  }
  PersistAllRuntimeQuiet(config_path, loops, stores);
  return 0;
}

void StopRunIfManagedDone(
    const std::map<std::string, mmtrade::strategy::SymbolLoopState> &loops,
    const std::map<std::string, std::unique_ptr<mmtrade::log::StrategyStore> > &stores,
    const std::string &reason) {
  if (!AllManagedDone(loops)) {
    return;
  }
  const std::string codes_label = JoinLoopCodes(loops);
  mmtrade::log::LogConsoleInfo("托管完毕，退出行情循环/managed_done_exit | reason=" + reason +
                               " | " + codes_label);
  for (std::map<std::string, mmtrade::strategy::SymbolLoopState>::const_iterator it =
           loops.begin();
       it != loops.end(); ++it) {
    mmtrade::log::StrategyStore *store = NULL;
    std::map<std::string, std::unique_ptr<mmtrade::log::StrategyStore> >::const_iterator sit =
        stores.find(it->first);
    if (sit != stores.end()) {
      store = sit->second.get();
    }
    if (store != NULL) {
      store->LogEvent("INFO", "managed_done_exit", reason + " " + it->first);
    }
  }
  g_run = 0;
}

bool ArgvHasFlag(int argc, char **argv, const char *flag) {
  if (flag == NULL) {
    return false;
  }
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == flag) {
      return true;
    }
  }
  return false;
}

int SessionFail(int code, const std::string &err) {
  g_session_error = err;
  if (!err.empty()) {
    std::cerr << err << "\n";
  }
  return code;
}

nlohmann::json FatalRunOutcomeJson() {
  if (!g_fatal_exit.active) {
    return nlohmann::json::object();
  }
  nlohmann::json j = {{"fatal", true},
                      {"code", g_fatal_exit.code.empty() ? "TRADING_FATAL" : g_fatal_exit.code},
                      {"symbol", g_fatal_exit.symbol},
                      {"detail", g_fatal_exit.detail}};
  return j;
}

bool WriteFdAll(int fd, const std::string &data) {
  size_t off = 0;
  while (off < data.size()) {
    const ssize_t n = ::write(fd, data.data() + off, data.size() - off);
    if (n < 0) {
      if (errno == EINTR) {
        if (g_stop_worker) {
          return false;
        }
        continue;
      }
      return false;
    }
    if (n == 0) {
      return false;
    }
    off += static_cast<size_t>(n);
  }
  return true;
}

bool ReadFdLine(int fd, std::string *out) {
  if (out == NULL) {
    return false;
  }
  out->clear();
  char ch = 0;
  while (true) {
    if (g_stop_worker) {
      return false;
    }
    const ssize_t n = ::read(fd, &ch, 1);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (n == 0) {
      return false;
    }
    if (ch == '\n') {
      return true;
    }
    out->push_back(ch);
    if (out->size() > 1024 * 1024) {
      return false;
    }
  }
}

class StdioAppendRedirect {
 public:
  StdioAppendRedirect() : saved_out_(-1), saved_err_(-1), active_(false) {}
  ~StdioAppendRedirect() { Restore(); }

  bool Begin(const std::string &path) {
    Restore();
    if (path.empty()) {
      return true;
    }
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
      return false;
    }
    std::fflush(stdout);
    std::fflush(stderr);
    saved_out_ = ::dup(STDOUT_FILENO);
    saved_err_ = ::dup(STDERR_FILENO);
    if (saved_out_ < 0 || saved_err_ < 0) {
      ::close(fd);
      Restore();
      return false;
    }
    ::dup2(fd, STDOUT_FILENO);
    ::dup2(fd, STDERR_FILENO);
    ::close(fd);
    active_ = true;
    return true;
  }

  void Restore() {
    if (!active_) {
      return;
    }
    std::fflush(stdout);
    std::fflush(stderr);
    if (saved_out_ >= 0) {
      ::dup2(saved_out_, STDOUT_FILENO);
      ::close(saved_out_);
      saved_out_ = -1;
    }
    if (saved_err_ >= 0) {
      ::dup2(saved_err_, STDERR_FILENO);
      ::close(saved_err_);
      saved_err_ = -1;
    }
    active_ = false;
  }

 private:
  int saved_out_;
  int saved_err_;
  bool active_;
};

struct LeaveOpendCleanup {
  mmtrade::opend::QuoteConnection *opend;
  std::vector<std::string> codes;
  bool active;

  LeaveOpendCleanup() : opend(NULL), active(false) {}
  ~LeaveOpendCleanup() {
    if (!active || opend == NULL) {
      return;
    }
    if (!codes.empty()) {
      opend->UnsubscribeOrderBook(codes);
    }
    opend->ClearQuoteCache();
  }
};

int RunStrategySession(AppArgs args, mmtrade::config::StrategyConfig cfg,
                       mmtrade::opend::QuoteConnection *shared_opend, bool leave_opend_up) {
  g_session_error.clear();
  if (args.host.empty()) {
    args.host = cfg.opend_host;
  }
  if (args.port <= 0) {
    args.port = cfg.opend_port;
  }

  const std::string mode = cfg.mode;
  const bool is_paper = (mode == "paper");
  if (mode != "paper" && mode != "real") {
    return SessionFail(2, "config mode must be paper or real, got: " + mode);
  }
  mmtrade::log::LogConsoleInfo("mode=" + mode + " (" + (is_paper ? "SIMULATE" : "REAL") + ")");
  if (args.dry_run) {
    mmtrade::log::LogConsoleWarn("dry-run: 只判条件不下单；去掉 --dry-run 才会报单");
  }

  const mmtrade::notify::TelegramSetupInfo tg_setup = mmtrade::notify::DescribeTelegramSetup(cfg);
  if (tg_setup.enabled) {
    mmtrade::log::LogConsoleInfo("telegram enabled token=" + tg_setup.token_masked +
                                 " chat_id=" + tg_setup.chat_id);
    mmtrade::log::LogConsoleInfo("telegram config=" + tg_setup.config_path +
                                 " secrets=" + tg_setup.secrets_path);
  } else {
    mmtrade::log::LogConsoleInfo("telegram disabled");
  }

  const std::string csv_dir = mmtrade::config::ResolveCsvDir(args.config_path, cfg.csv_dir);
  const std::string trd_env = mmtrade::notify::ResolveTrdEnvLabel(cfg);
  mmtrade::log::LogConsoleInfo("csv_dir=" + csv_dir);
  if (mmtrade::task::TaskLatencyRecorder::Instance().Enabled()) {
    mmtrade::task::TaskLatencyRecorder &lat = mmtrade::task::TaskLatencyRecorder::Instance();
    lat.RecordValue("acc_id", static_cast<int64_t>(cfg.acc_id));
    lat.RecordValue("acc_index", static_cast<int64_t>(cfg.acc_index));
    lat.RecordString("latency_csv_dir", csv_dir);
  }

  std::map<std::string, mmtrade::strategy::SymbolLoopState> loops;
  std::map<std::string, std::unique_ptr<mmtrade::strategy::LiveEvalCompute> > live_compute;
  std::map<std::string, std::unique_ptr<mmtrade::log::StrategyStore> > stores;
  bool latency_symbol_set = false;
  for (size_t i = 0; i < cfg.symbols.size(); ++i) {
    if (!cfg.symbols[i].enabled) {
      continue;
    }
    mmtrade::strategy::SymbolLoopState st;
    st.sym = cfg.symbols[i];
    InitLoopState(&st);
    loops[st.sym.code] = st;
    if (!latency_symbol_set && mmtrade::task::TaskLatencyRecorder::Instance().Enabled() &&
        !st.sym.code.empty()) {
      mmtrade::task::TaskLatencyRecorder::Instance().RecordString("symbol", st.sym.code);
      latency_symbol_set = true;
    }
    stores[st.sym.code].reset(
        new mmtrade::log::StrategyStore(csv_dir, st.sym.code, cfg.acc_id, cfg.acc_index));
    RunStartupHooks(&loops[st.sym.code], cfg, args.config_path, trd_env, stores[st.sym.code].get());
  }
  if (loops.empty()) {
    return SessionFail(0, "no enabled symbols");
  }
  {
    std::map<std::string, std::unique_ptr<mmtrade::log::StrategyStore> >::iterator sit =
        stores.begin();
    if (sit != stores.end() && sit->second.get() != NULL) {
      mmtrade::log::LogTrdContext(sit->second.get(), cfg, trd_env);
    }
  }

  {
    const int early = ExitIfManagedDoneAtStartup(args.config_path, loops, stores);
    if (early >= 0) {
      return early;
    }
  }

  std::signal(SIGINT, OnSignal);
  std::signal(SIGTERM, OnSignal);

  mmtrade::opend::QuoteConnection local_opend;
  mmtrade::opend::QuoteConnection *opend =
      (shared_opend != NULL) ? shared_opend : &local_opend;
  const bool own_opend = (shared_opend == NULL);

  LeaveOpendCleanup leave_cleanup;
  if (leave_opend_up && opend != NULL) {
    leave_cleanup.opend = opend;
    leave_cleanup.active = true;
  }

  if (own_opend) {
    Moomoo::MMAPI::Init();
    if (!opend->Connect(args.host, args.port)) {
      google::protobuf::ShutdownProtobufLibrary();
      Moomoo::MMAPI::UnInit();
      return SessionFail(1, "quote connect failed");
    }
  } else if (!opend->quote_connected()) {
    return SessionFail(1, "quote connect failed");
  }
  if (mmtrade::task::TaskLatencyRecorder::Instance().Enabled()) {
    mmtrade::task::TaskLatencyRecorder::Instance().RecordTimestampMs("opend_connected_ms");
  }

  std::vector<std::string> codes;
  for (std::map<std::string, mmtrade::strategy::SymbolLoopState>::iterator it = loops.begin();
       it != loops.end(); ++it) {
    codes.push_back(it->first);
  }
  leave_cleanup.codes = codes;
  if (!opend->SubscribeOrderBook(codes)) {
    if (own_opend && !leave_opend_up) {
      opend->Disconnect();
      google::protobuf::ShutdownProtobufLibrary();
      Moomoo::MMAPI::UnInit();
    }
    return SessionFail(1, "orderbook subscribe failed");
  }
  if (mmtrade::task::TaskLatencyRecorder::Instance().Enabled()) {
    mmtrade::task::TaskLatencyRecorder::Instance().RecordTimestampMs("subscribe_done_ms");
  }

  for (std::map<std::string, mmtrade::strategy::SymbolLoopState>::iterator it = loops.begin();
       it != loops.end(); ++it) {
    const std::string &code = it->first;
    opend->RefreshOrderBook(code);
    opend->FetchDayOpenPrice(code);
    const mmtrade::opend::QuoteSnapshot snap = opend->GetSnapshot(code);
    it->second.eval_rt.day_open_gain.day_open = snap.day_open;
    if (snap.lot_size > 0) {
      it->second.lot_size = snap.lot_size;
    }
    if (mmtrade::strategy::SymbolNeedsLiveEval(it->second.sym)) {
      live_compute[code].reset(
          new mmtrade::strategy::LiveEvalCompute(args.host, args.port, code));
    }
    mmtrade::log::LogConsoleInfo("init " + code + " status=" + mmtrade::log::StatusLabel(it->second.sym.status) +
                                 " lot_size=" + std::to_string(static_cast<long long>(it->second.lot_size)) +
                                 " opened_qty=" +
                                 std::to_string(static_cast<long long>(it->second.sym.opened_qty)) +
                                 " exit_enabled=" + mmtrade::log::BoolMark(it->second.sym.exit.enabled));
    mmtrade::log::LogConsoleInfo("logs " + stores[code]->runtime_log_path());
    mmtrade::log::LogLotSize(stores[code].get(), code, static_cast<int>(it->second.lot_size));
    mmtrade::log::LogStrategyReady(stores[code].get(), code, tg_setup.enabled,
                                   it->second.sym.startup_notify_once);
  }

  std::unique_ptr<mmtrade::opend::OrderClient> order_client;
  std::unique_ptr<mmtrade::trade::IOrderBroker> place_broker;
  bool order_ready = false;
  if (!args.dry_run) {
    mmtrade::opend::OrderConfig oc;
    oc.host = args.host;
    oc.port = args.port;
    oc.simulate = is_paper;
    oc.acc_id = static_cast<std::uint64_t>(cfg.acc_id);
    order_client.reset(new mmtrade::opend::OrderClient(oc, opend));
    const bool same_trade =
        leave_opend_up && opend->trade_connected() && g_pool_trade.valid &&
        g_pool_trade.acc_id == oc.acc_id && g_pool_trade.simulate == is_paper;
    if (same_trade) {
      opend->set_simulate(is_paper);
      if (oc.acc_id != 0) {
        opend->set_prefer_acc_id(oc.acc_id);
      }
      order_ready = true;
    } else {
      order_ready = order_client->Connect();
      if (order_ready && leave_opend_up) {
        g_pool_trade.valid = true;
        g_pool_trade.acc_id = oc.acc_id;
        g_pool_trade.simulate = is_paper;
      } else if (!order_ready && leave_opend_up) {
        g_pool_trade.valid = false;
      }
    }
    if (!order_ready) {
      const std::string connect_warn =
          "order client connect failed (real 需 MOOMOO_TRADE_PWD 或 .env.real 中 Fernet 路径)";
      mmtrade::log::LogConsoleWarn(connect_warn);
      for (std::map<std::string, std::unique_ptr<mmtrade::log::StrategyStore> >::iterator sit =
               stores.begin();
           sit != stores.end(); ++sit) {
        if (sit->second.get() != NULL) {
          sit->second->LogEvent("WARN", "order_client_connect_failed", connect_warn);
        }
      }
    } else {
      order_client->EnsurePushHub();
      mmtrade::trade::DirectBrokerConfig dcfg;
      dcfg.host = args.host;
      dcfg.port = args.port;
      dcfg.simulate = is_paper;
      dcfg.acc_id = static_cast<std::uint64_t>(cfg.acc_id);
      place_broker =
          mmtrade::opend::CreateOrderBroker(dcfg, order_client->TradeConn());
      if (place_broker.get() != NULL) {
        order_client->SetPlaceBroker(place_broker.get());
        mmtrade::log::LogConsoleInfo(
            std::string("trade connected env=") + trd_env +
            " order_push=on place_via=" + mmtrade::opend::OrderBrokerModeFromEnv() +
            " gateway=" + mmtrade::opend::GatewayUrlFromEnv());
      } else {
        mmtrade::log::LogConsoleInfo("trade connected env=" + trd_env +
                                     " order_push=on place_via=direct");
      }
      if (mmtrade::task::TaskLatencyRecorder::Instance().Enabled()) {
        mmtrade::task::TaskLatencyRecorder::Instance().RecordTimestampMs("order_client_ready_ms");
      }
    }
  }

  mmtrade::strategy::FillHooks fill_hooks;
  fill_hooks.cfg = &cfg;
  fill_hooks.config_path = args.config_path;
  fill_hooks.trd_env = trd_env;

  bool latency_first_push = false;
  bool latency_first_judge = false;
  bool latency_entry_hit = false;
  bool latency_exit_hit = false;
  bool latency_live_eval = false;

  // Init already Subscribe+RefreshOrderBook; skip the first WaitBookPush if cache is warm
  // so order_client_ready → first_book is not capped by kBookPollSec (0.5s).
  bool book_cache_ready = false;
  for (std::map<std::string, mmtrade::strategy::SymbolLoopState>::iterator it = loops.begin();
       it != loops.end(); ++it) {
    if (opend->GetSnapshot(it->first).valid) {
      book_cache_ready = true;
      break;
    }
  }
  if (book_cache_ready && mmtrade::task::TaskLatencyRecorder::Instance().Enabled()) {
    latency_first_push = true;
    mmtrade::task::TaskLatencyRecorder::Instance().RecordTimestampMs("first_book_push_ms");
  }

  bool first_loop = true;
  int tick_count = 0;
  double last_market_state_poll_ts = 0.0;
  while (g_run && (args.max_ticks <= 0 || tick_count < args.max_ticks)) {
    const bool awaiting_fill = AnyAwaitingFill(loops);
    const double book_wait_sec =
        awaiting_fill ? kBookPollSec : std::max(kBookPollSec, args.poll_sec);
    const bool skip_first_book_wait = first_loop && book_cache_ready;
    if (!skip_first_book_wait) {
      opend->WaitBookPush(book_wait_sec);
    }
    mmtrade::task::PipelineLatency::OnMdLoopWakeup();
    first_loop = false;
    if (mmtrade::task::TaskLatencyRecorder::Instance().Enabled() && !latency_first_push) {
      latency_first_push = true;
      mmtrade::task::TaskLatencyRecorder::Instance().RecordTimestampMs("first_book_push_ms");
    }

    const double now = NowUnix();
    const double now_mono = NowMono();
    RefreshAndApplyMarketPhases(opend, &loops, now, &last_market_state_poll_ts);
    if (ShouldAutoStopAfterClose(cfg, loops, opend)) {
      mmtrade::log::LogConsoleInfo("自动停机/auto_stop_after_close | all_markets_post_close");
      g_run = 0;
      break;
    }
    const double trad_poll_sec = mmtrade::strategy::SymbolTradabilityPollSec();
    for (std::map<std::string, mmtrade::strategy::SymbolLoopState>::iterator it = loops.begin();
         it != loops.end(); ++it) {
      mmtrade::strategy::SymbolLoopState &st = it->second;
      fill_hooks.store = stores[st.sym.code].get();

      if (st.sym.status >= 3 && st.exit_done) {
        continue;
      }

      mmtrade::opend::QuoteSnapshot snap = opend->GetSnapshot(st.sym.code);
      const bool need_tradability_snap =
          !snap.has_tradability ||
          (now - st.last_symbol_tradability_poll_ts >= trad_poll_sec);
      if (need_tradability_snap) {
        opend->RefreshMarketSnapshot(st.sym.code);
        snap = opend->GetSnapshot(st.sym.code);
      }
      if (!snap.valid) {
        opend->RefreshOrderBook(st.sym.code);
        snap = opend->GetSnapshot(st.sym.code);
      }
      const mmtrade::strategy::BookTick book = ToBookTick(snap);
      if (st.eval_rt.day_open_gain.day_open <= 0.0 && snap.has_day_open) {
        st.eval_rt.day_open_gain.day_open = snap.day_open;
      }

      if (!args.dry_run && order_ready) {
        const mmtrade::strategy::PendingAdvanceResult adv =
            mmtrade::strategy::AdvancePendingOrder(&st, cfg, order_client.get(), book, snap, now,
                                                  now_mono, &fill_hooks);
        if (adv == mmtrade::strategy::PendingAdvanceResult::kFatal ||
            !st.trading_fatal_code.empty()) {
          RequestTradingFatalStop(st, "advance_pending");
          break;
        }
      }

      if (!st.trading_fatal_code.empty() || st.entry_blocked) {
        RequestTradingFatalStop(st, "pre_judge");
        break;
      }

      const mmtrade::strategy::StrategyTickContext tick =
          mmtrade::strategy::BuildTickContext(st.sym, book, now, st.sym.code);

      mmtrade::strategy::LiveEvalCompute *live = NULL;
      std::map<std::string, std::unique_ptr<mmtrade::strategy::LiveEvalCompute> >::iterator lit =
          live_compute.find(st.sym.code);
      if (lit != live_compute.end()) {
        if (mmtrade::task::TaskLatencyRecorder::Instance().Enabled() && !latency_live_eval) {
          latency_live_eval = true;
          mmtrade::task::TaskLatencyRecorder::Instance().RecordTimestampMs("live_eval_first_ms");
        }
        lit->second->ResetTick();
        live = lit->second.get();
      }

      const mmtrade::strategy::SymbolJudgeOutcome outcome =
          mmtrade::strategy::RunSymbolJudge(st.sym, tick, &st.eval_rt, live);
      if (mmtrade::task::TaskLatencyRecorder::Instance().Enabled()) {
        mmtrade::task::TaskLatencyRecorder::Instance().Increment("judge_total_count");
        if (!latency_first_judge) {
          latency_first_judge = true;
          mmtrade::task::TaskLatencyRecorder::Instance().RecordTimestampMs("first_judge_ms");
        }
        if (outcome.entry_hit && !latency_entry_hit) {
          latency_entry_hit = true;
          mmtrade::task::TaskLatencyRecorder::Instance().RecordTimestampMs("entry_judge_hit_ms");
          mmtrade::task::PipelineLatency::OnEntryStrategyDone();
        }
        if (outcome.exit_hit && !latency_exit_hit) {
          latency_exit_hit = true;
          mmtrade::task::TaskLatencyRecorder::Instance().RecordTimestampMs("exit_judge_hit_ms");
          mmtrade::task::PipelineLatency::OnExitStrategyDone();
        }
      }

      std::ostringstream summary;
      summary << "[" << st.sym.code << "] status=" << mmtrade::log::StatusLabel(st.sym.status)
              << " bid=" << book.bid_1 << " ask=" << book.ask_1 << " | entry="
              << (outcome.entry_hit ? outcome.entry_rule_id : "-") << "("
              << mmtrade::log::BoolMark(outcome.entry_hit) << ")"
              << " exit=" << (outcome.exit_hit ? outcome.exit_rule_id : "-") << "("
              << mmtrade::log::BoolMark(outcome.exit_hit) << ")"
              << " await_fill=" << mmtrade::log::BoolMark(mmtrade::strategy::IsAwaitingFill(st));
      if (st.sym.status >= 2 && st.sym.opened_qty > 0) {
        summary << " pos=" << st.sym.opened_qty << "@" << st.sym.opened_price;
      }
      mmtrade::log::StrategyStore *sym_store = stores[st.sym.code].get();
      if (sym_store != NULL && book.valid) {
        mmtrade::log::LogReadMarket(sym_store, st.sym.code, book.bid_1, book.ask_1);
      }
      if (sym_store != NULL && outcome.entry_hit && book.valid) {
        const double px = book.ask_1 > 0.0 ? book.ask_1 : book.bid_1;
        std::string note;
        const double qty =
            mmtrade::strategy::ResolveEntryQtyForPlace(st.sym, px, st.lot_size, &note);
        mmtrade::log::LogEntryJudge(sym_store, st.sym.code, outcome.entry_rule_id, 0, "orderbook",
                                    "op_empty_skip_compare", true, qty, px * qty);
      }

      const char *tick_console = std::getenv("MOOMOO_STRATEGY_CONSOLE_TICK");
      if (tick_console != NULL && tick_console[0] != '\0' && tick_console[0] != '0') {
        mmtrade::log::LogConsoleInfo(summary.str());
      }

      if (args.verbose_json) {
        std::ostringstream line;
        line << "{\"ts\":" << now << ",\"code\":\"" << JsonEscape(st.sym.code) << "\""
             << ",\"status\":" << st.sym.status << ",\"bid\":" << book.bid_1 << ",\"ask\":" << book.ask_1
             << ",\"entry_hit\":" << (outcome.entry_hit ? "true" : "false")
             << ",\"entry_rule\":\"" << JsonEscape(outcome.entry_rule_id) << "\""
             << ",\"exit_hit\":" << (outcome.exit_hit ? "true" : "false")
             << ",\"exit_rule\":\"" << JsonEscape(outcome.exit_rule_id) << "\""
             << ",\"await_fill\":" << (mmtrade::strategy::IsAwaitingFill(st) ? "true" : "false")
             << ",\"dry_run\":" << (args.dry_run ? "true" : "false") << "}";
        std::cout << line.str() << std::endl;
      }

      if (args.dry_run || !order_ready) {
        continue;
      }

      const std::string order_sess =
          st.sym.order_session.empty() ? "RTH" : st.sym.order_session;
      const bool phase_allows =
          mmtrade::strategy::PhaseAllowsTrading(order_sess, st.last_market_phase);
      const bool can_place_entry =
          outcome.entry_hit && !st.entry_await_fill && !mmtrade::strategy::IsAwaitingFill(st) &&
          !st.entry_done && !st.entry_blocked && st.trading_fatal_code.empty() &&
          st.sym.status < 2 && phase_allows;
      const bool can_place_exit =
          outcome.exit_hit && !st.exit_await_fill && !mmtrade::strategy::IsAwaitingFill(st) &&
          st.entry_done && !st.exit_done && !st.entry_blocked && st.trading_fatal_code.empty() &&
          st.sym.status == 2 && phase_allows;

      if (can_place_entry) {
        st.last_entry_hit_rule_id = outcome.entry_rule_id;
        if (sym_store != NULL && book.valid) {
          std::string note;
          const double px = book.ask_1 > 0.0 ? book.ask_1 : book.bid_1;
          const double qty =
              mmtrade::strategy::ResolveEntryQtyForPlace(st.sym, px, st.lot_size, &note);
          mmtrade::log::LogEntryJudge(sym_store, st.sym.code, outcome.entry_rule_id, 0,
                                      "orderbook", "op_empty_skip_compare", true, qty, px * qty);
          mmtrade::log::LogStartTrade(
              sym_store, st.sym.code,
              mmtrade::log::FormatEntryRuleHitLabel(st.sym, outcome.entry_rule_id));
        }
        mmtrade::strategy::TryPlaceEntry(&st, cfg, order_client.get(), book, snap, now, now_mono,
                                        sym_store, trd_env);
        if (!st.trading_fatal_code.empty() || st.entry_blocked) {
          RequestTradingFatalStop(st, "place_entry");
          break;
        }
      }
      if (can_place_exit) {
        if (sym_store != NULL && book.valid) {
          const double px = book.bid_1 > 0.0 ? book.bid_1 : book.ask_1;
          const double qty = mmtrade::strategy::ResolveExitQty(st.sym);
          mmtrade::log::LogExitJudge(sym_store, st.sym.code, outcome.exit_rule_id, 0,
                                     "orderbook", "op_empty_skip_compare", true, qty, px * qty);
        }
        mmtrade::strategy::TryPlaceExit(&st, cfg, order_client.get(), book, snap, now, now_mono,
                                        outcome.exit_rule_id, sym_store, trd_env);
        if (!st.trading_fatal_code.empty() || st.entry_blocked) {
          RequestTradingFatalStop(st, "place_exit");
          break;
        }
      }
    }

    StopRunIfManagedDone(loops, stores, "loop_tick");

    ++tick_count;
    if (mmtrade::task::TaskLatencyRecorder::Instance().Enabled()) {
      mmtrade::task::TaskLatencyRecorder::Instance().RecordValue("tick_count", tick_count);
    }
  }

  if (g_fatal_exit.active) {
    WriteRunOutcome(args.config_path, g_fatal_exit);
  }

  PersistAllRuntimeQuiet(args.config_path, loops, stores);

  if (mmtrade::task::TaskLatencyRecorder::Instance().Enabled()) {
    mmtrade::task::TaskLatencyRecorder::Instance().RecordTimestampMs("strategy_exit_ms");
    mmtrade::task::TaskLatencyRecorder::Instance().FlushAndWait();
  }

  for (std::map<std::string, std::unique_ptr<mmtrade::strategy::LiveEvalCompute> >::iterator it =
           live_compute.begin();
       it != live_compute.end(); ++it) {
    if (it->second.get() != NULL) {
      it->second->Close();
    }
  }
  if (order_client.get() != NULL) {
    order_client->Close();
  }
  order_client.reset();
  place_broker.reset();
  if (!leave_opend_up) {
    opend->DisconnectTrade();
    opend->Disconnect();
    google::protobuf::ShutdownProtobufLibrary();
    Moomoo::MMAPI::UnInit();
  }
  if (g_fatal_exit.active) {
    return kExitTradingFatal;
  }
  return 0;
}

void PoolWorkerShutdown(mmtrade::opend::QuoteConnection *opend, int listen_fd, int client_fd,
                        const std::string &sock_path) {
  if (client_fd >= 0) {
    ::close(client_fd);
  }
  if (listen_fd >= 0) {
    ::close(listen_fd);
  }
  if (opend != NULL) {
    opend->DisconnectTrade();
    opend->Disconnect();
  }
  google::protobuf::ShutdownProtobufLibrary();
  Moomoo::MMAPI::UnInit();
  if (!sock_path.empty()) {
    ::unlink(sock_path.c_str());
  }
}

int PoolWorkerMain(int argc, char **argv) {
  std::string sock_path;
  int pool_id = 0;
  std::string host = "127.0.0.1";
  int port = 11121;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--pool-worker") {
      continue;
    }
    if (a == "--pool-sock" && i + 1 < argc) {
      sock_path = argv[++i];
    } else if (a == "--pool-id" && i + 1 < argc) {
      pool_id = std::atoi(argv[++i]);
    } else if (a == "--host" && i + 1 < argc) {
      host = argv[++i];
    } else if (a == "--port" && i + 1 < argc) {
      port = std::atoi(argv[++i]);
    }
  }
  if (sock_path.empty()) {
    std::cerr << "usage: strategy_cpp --pool-worker --pool-sock PATH [--pool-id N] "
                 "[--host H] [--port P]\n";
    return 2;
  }

  g_run = 1;
  g_stop_worker = 0;
  std::signal(SIGINT, OnSignal);
  std::signal(SIGTERM, OnSignal);
  std::signal(SIGPIPE, SIG_IGN);

  ::unlink(sock_path.c_str());
  const int listen_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    std::cerr << "pool-worker socket() failed errno=" << errno << "\n";
    return 1;
  }
  sockaddr_un addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  if (sock_path.size() >= sizeof(addr.sun_path)) {
    std::cerr << "pool-sock path too long\n";
    ::close(listen_fd);
    return 2;
  }
  std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sock_path.c_str());
  if (::bind(listen_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    std::cerr << "pool-worker bind() failed path=" << sock_path << " errno=" << errno << "\n";
    ::close(listen_fd);
    return 1;
  }
  if (::listen(listen_fd, 1) != 0) {
    std::cerr << "pool-worker listen() failed errno=" << errno << "\n";
    ::close(listen_fd);
    ::unlink(sock_path.c_str());
    return 1;
  }

  Moomoo::MMAPI::Init();
  mmtrade::opend::QuoteConnection opend;
  if (!opend.Connect(host, port)) {
    std::cerr << "pool-worker quote connect failed host=" << host << " port=" << port << "\n";
    google::protobuf::ShutdownProtobufLibrary();
    Moomoo::MMAPI::UnInit();
    ::close(listen_fd);
    ::unlink(sock_path.c_str());
    return 1;
  }
  mmtrade::log::LogConsoleInfo("pool-worker ready id=" + std::to_string(static_cast<long long>(pool_id)) +
                               " sock=" + sock_path + " host=" + host +
                               " port=" + std::to_string(static_cast<long long>(port)));

  int client_fd = -1;
  while (!g_stop_worker && client_fd < 0) {
    client_fd = ::accept(listen_fd, NULL, NULL);
    if (client_fd < 0) {
      if (errno == EINTR) {
        continue;
      }
      std::cerr << "pool-worker accept() failed errno=" << errno << "\n";
      PoolWorkerShutdown(&opend, listen_fd, -1, sock_path);
      return 1;
    }
  }
  ::close(listen_fd);
  if (client_fd < 0) {
    PoolWorkerShutdown(&opend, -1, -1, sock_path);
    return 0;
  }

  while (!g_stop_worker) {
    std::string line;
    if (!ReadFdLine(client_fd, &line)) {
      break;
    }
    nlohmann::json req;
    try {
      req = nlohmann::json::parse(line);
    } catch (const std::exception &e) {
      const nlohmann::json bad = {{"ok", false},
                                  {"exit_code", 2},
                                  {"error", std::string("bad json: ") + e.what()},
                                  {"run_outcome", nlohmann::json::object()}};
      if (!WriteFdAll(client_fd, bad.dump() + "\n")) {
        break;
      }
      continue;
    }
    const std::string cmd = req.value("cmd", "");
    if (cmd == "ping") {
      const nlohmann::json pong = {{"ok", true}, {"pong", true}};
      if (!WriteFdAll(client_fd, pong.dump() + "\n")) {
        break;
      }
      continue;
    }
    if (cmd == "shutdown") {
      break;
    }
    if (cmd != "run") {
      const nlohmann::json unk = {{"ok", false},
                                  {"exit_code", 2},
                                  {"error", "unknown cmd: " + cmd},
                                  {"run_outcome", nlohmann::json::object()}};
      if (!WriteFdAll(client_fd, unk.dump() + "\n")) {
        break;
      }
      continue;
    }

    const std::string config_path = req.value("config", "");
    const std::string latency_staging = req.value("latency_staging", "");
    const std::string latency_csv = req.value("latency_csv", "");
    const std::string run_log = req.value("run_log", "");
    if (config_path.empty()) {
      const nlohmann::json miss = {{"ok", false},
                                   {"exit_code", 2},
                                   {"error", "missing config"},
                                   {"run_outcome", nlohmann::json::object()}};
      if (!WriteFdAll(client_fd, miss.dump() + "\n")) {
        break;
      }
      continue;
    }

    if (!latency_staging.empty()) {
      ::setenv("MOOMOO_TASK_LATENCY_STAGING", latency_staging.c_str(), 1);
    } else {
      ::unsetenv("MOOMOO_TASK_LATENCY_STAGING");
    }
    if (!latency_csv.empty()) {
      ::setenv("MOOMOO_TASK_LATENCY_CSV", latency_csv.c_str(), 1);
    } else {
      ::unsetenv("MOOMOO_TASK_LATENCY_CSV");
    }
    mmtrade::task::TaskLatencyRecorder::ConfigureFromEnv();
    if (latency_staging.empty()) {
      mmtrade::task::TaskLatencyRecorder::Instance().Configure("", "");
    }
    g_run = 1;
    g_fatal_exit = TradingFatalExit();
    g_session_error.clear();

    StdioAppendRedirect stdio_redir;
    if (!run_log.empty()) {
      if (!stdio_redir.Begin(run_log)) {
        mmtrade::log::LogConsoleWarn("pool-worker run_log open failed path=" + run_log);
      } else {
        std::cout << "=== pool-worker session start id=" << pool_id
                  << " config=" << config_path << " ===\n";
      }
    }

    int exit_code = 2;
    mmtrade::config::StrategyConfig cfg;
    try {
      cfg = mmtrade::config::LoadConfig(config_path);
      if (mmtrade::task::TaskLatencyRecorder::Instance().Enabled()) {
        mmtrade::task::TaskLatencyRecorder::Instance().RecordTimestampMs("strategy_main_start_ms");
        mmtrade::task::TaskLatencyRecorder::Instance().RecordTimestampMs("config_loaded_ms");
      }
      AppArgs args;
      args.config_path = config_path;
      args.host = host;
      args.port = port;
      exit_code = RunStrategySession(args, cfg, &opend, true);
    } catch (const std::exception &e) {
      g_session_error = e.what();
      std::cerr << e.what() << "\n";
      exit_code = 2;
    }

    if (!run_log.empty()) {
      std::cout << "=== pool-worker session end id=" << pool_id << " exit=" << exit_code
                << " ===\n";
      stdio_redir.Restore();
    }

    nlohmann::json resp;
    resp["ok"] = (exit_code == 0);
    resp["exit_code"] = exit_code;
    resp["error"] = g_session_error;
    resp["run_outcome"] = FatalRunOutcomeJson();
    if (!WriteFdAll(client_fd, resp.dump() + "\n")) {
      break;
    }
    if (g_stop_worker) {
      break;
    }
  }

  PoolWorkerShutdown(&opend, -1, client_fd, sock_path);
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  if (ArgvHasFlag(argc, argv, "--pool-worker")) {
    return PoolWorkerMain(argc, argv);
  }

  mmtrade::task::TaskLatencyRecorder::ConfigureFromEnv();
  if (mmtrade::task::TaskLatencyRecorder::Instance().Enabled()) {
    mmtrade::task::TaskLatencyRecorder::Instance().RecordTimestampMs("strategy_main_start_ms");
  }

  const mmtrade::config::ConfigPathResolveResult cfg_path =
      mmtrade::config::ResolveStrategyConfigPath(argc, argv);
  if (!cfg_path.ok) {
    std::cerr << cfg_path.error << "\n";
    std::cerr << "usage: strategy_cpp [-us|-hk|-c PATH|CONFIG.json] "
                 "[--poll-sec N] [--max-ticks N] [--dry-run] [--verbose-json]\n";
    return 2;
  }
  mmtrade::log::LogConsoleInfo("strategy_config=" + cfg_path.path);

  AppArgs args;
  args.config_path = cfg_path.path;
  ParseAppFlags(argc, argv, &args);

  mmtrade::config::StrategyConfig cfg;
  try {
    cfg = mmtrade::config::LoadConfig(args.config_path);
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 2;
  }
  if (mmtrade::task::TaskLatencyRecorder::Instance().Enabled()) {
    mmtrade::task::TaskLatencyRecorder::Instance().RecordTimestampMs("config_loaded_ms");
  }
  return RunStrategySession(args, cfg, NULL, false);
}
