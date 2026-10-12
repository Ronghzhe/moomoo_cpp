#include "task/strategy_worker_pool.hpp"

#include "task/task_paths.hpp"

#include <cerrno>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

namespace mmtrade {
namespace task {
namespace {

std::string JoinPath(const std::string &a, const std::string &b) {
  if (a.empty()) {
    return b;
  }
  if (a[a.size() - 1] == '/') {
    return a + b;
  }
  return a + "/" + b;
}

std::string StrategyCppBinPath() {
  const char *env = std::getenv("MOOMOO_STRATEGY_CPP_BIN");
  if (env != NULL && env[0] != '\0') {
    return std::string(env);
  }
  const std::string root =
      (std::getenv("MOOMOO_CPP_ROOT") != NULL && std::getenv("MOOMOO_CPP_ROOT")[0] != '\0')
          ? std::string(std::getenv("MOOMOO_CPP_ROOT"))
          : std::string("/home/ubuntu/workspace/moomoo_CPP");
  return JoinPath(root, "build/apps/strategy_cpp");
}

bool WriteAll(int fd, const std::string &data) {
  size_t off = 0;
  while (off < data.size()) {
    const ssize_t n = ::write(fd, data.data() + off, data.size() - off);
    if (n < 0) {
      if (errno == EINTR) {
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

bool ReadLine(int fd, std::string *out, int timeout_ms) {
  if (out == NULL) {
    return false;
  }
  out->clear();
  char buf[1];
  const double deadline =
      static_cast<double>(time(NULL)) + (timeout_ms > 0 ? timeout_ms / 1000.0 : 86400.0 * 30);
  while (true) {
    if (timeout_ms > 0) {
      const double now = static_cast<double>(time(NULL));
      if (now >= deadline) {
        return false;
      }
      pollfd pfd;
      pfd.fd = fd;
      pfd.events = POLLIN;
      pfd.revents = 0;
      const int remain_ms = static_cast<int>((deadline - now) * 1000.0);
      const int pr = ::poll(&pfd, 1, remain_ms > 0 ? remain_ms : 0);
      if (pr == 0) {
        return false;
      }
      if (pr < 0) {
        if (errno == EINTR) {
          continue;
        }
        return false;
      }
    }
    const ssize_t n = ::read(fd, buf, 1);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (n == 0) {
      return !out->empty();
    }
    if (buf[0] == '\n') {
      return true;
    }
    out->push_back(buf[0]);
    if (out->size() > 1024 * 1024) {
      return false;
    }
  }
}

}  // namespace

bool StrategyPoolEnabledFromEnv() {
  const char *raw = std::getenv("MOOMOO_STRATEGY_POOL");
  if (raw == NULL || raw[0] == '\0') {
    return true;
  }
  const std::string v(raw);
  if (v == "0" || v == "false" || v == "no" || v == "off" || v == "OFF" || v == "FALSE" ||
      v == "NO") {
    return false;
  }
  return true;
}

StrategyWorkerPool::StrategyWorkerPool()
    : enabled_(false), task_type_(0), worker_count_(0), rr_(0), log_(NULL) {}

StrategyWorkerPool::~StrategyWorkerPool() { Stop(); }

bool StrategyWorkerPool::Start(int size, int task_type, SystemLogger *log) {
  Stop();
  log_ = log;
  task_type_ = task_type;
  bin_path_ = StrategyCppBinPath();
  sock_dir_ = JoinPath(DefaultTaskSystemDir(), "pool_socks");
  ::mkdir(DefaultTaskSystemDir().c_str(), 0755);
  ::mkdir(sock_dir_.c_str(), 0755);

  if (size < 1) {
    size = 1;
  }
  if (::access(bin_path_.c_str(), X_OK) != 0) {
    if (log != NULL) {
      log->Error("strategy_pool_bin_missing path=" + bin_path_);
    }
    return false;
  }

  workers_.resize(static_cast<size_t>(size));
  worker_count_ = size;
  int ok_n = 0;
  for (int i = 0; i < size; ++i) {
    workers_[static_cast<size_t>(i)].id = i;
    if (SpawnOne(i, log)) {
      ++ok_n;
    }
  }
  enabled_ = ok_n > 0;
  if (log != NULL) {
    std::ostringstream oss;
    oss << "strategy_pool_start size=" << size << " ready=" << ok_n
        << " task_type=" << task_type_ << " bin=" << bin_path_;
    if (enabled_) {
      log->Info(oss.str());
    } else {
      log->Error(oss.str());
    }
  }
  return enabled_;
}

void StrategyWorkerPool::Stop() {
  {
    std::lock_guard<std::mutex> lk(mu_);
    for (size_t i = 0; i < workers_.size(); ++i) {
      Worker &w = workers_[i];
      if (w.fd >= 0) {
        const std::string bye = "{\"cmd\":\"shutdown\"}\n";
        (void)WriteAll(w.fd, bye);
      }
      KillWorker(&w);
    }
    workers_.clear();
    enabled_ = false;
    worker_count_ = 0;
    rr_ = 0;
  }
  cv_.notify_all();
}

bool StrategyWorkerPool::SpawnOne(int id, SystemLogger *log) {
  if (id < 0 || static_cast<size_t>(id) >= workers_.size()) {
    return false;
  }
  Worker &w = workers_[static_cast<size_t>(id)];
  KillWorker(&w);
  w.id = id;
  std::ostringstream sock_oss;
  sock_oss << sock_dir_ << "/t" << task_type_ << "_w" << id << ".sock";
  w.sock_path = sock_oss.str();
  ::unlink(w.sock_path.c_str());

  const pid_t pid = ::fork();
  if (pid < 0) {
    if (log != NULL) {
      log->Error("strategy_pool_fork_failed id=" + std::to_string(id));
    }
    return false;
  }
  if (pid == 0) {
    std::ostringstream id_oss;
    id_oss << id;
    execl(bin_path_.c_str(), bin_path_.c_str(), "--pool-worker", "--pool-sock",
          w.sock_path.c_str(), "--pool-id", id_oss.str().c_str(), (char *)NULL);
    _exit(127);
  }
  w.pid = pid;
  w.busy = false;
  if (!WaitReady(&w, 20000, log)) {
    KillWorker(&w);
    return false;
  }
  return true;
}

bool StrategyWorkerPool::WaitReady(Worker *w, int timeout_ms, SystemLogger *log) {
  if (w == NULL) {
    return false;
  }
  const double deadline = static_cast<double>(time(NULL)) + timeout_ms / 1000.0;
  while (static_cast<double>(time(NULL)) < deadline) {
    int st = 0;
    const pid_t rp = ::waitpid(w->pid, &st, WNOHANG);
    if (rp == w->pid) {
      if (log != NULL) {
        log->Error("strategy_pool_worker_exited_early id=" + std::to_string(w->id) +
                   " status=" + std::to_string(st));
      }
      w->pid = -1;
      return false;
    }
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
      usleep(50000);
      continue;
    }
    sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", w->sock_path.c_str());
    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0) {
      w->fd = fd;
      if (!WriteAll(fd, "{\"cmd\":\"ping\"}\n")) {
        ::close(fd);
        w->fd = -1;
        usleep(50000);
        continue;
      }
      std::string line;
      if (!ReadLine(fd, &line, 5000)) {
        ::close(fd);
        w->fd = -1;
        usleep(50000);
        continue;
      }
      try {
        const nlohmann::json j = nlohmann::json::parse(line);
        if (j.value("ok", false) && j.value("pong", false)) {
          w->alive = true;
          return true;
        }
      } catch (...) {
      }
      ::close(fd);
      w->fd = -1;
    } else {
      ::close(fd);
    }
    usleep(50000);
  }
  if (log != NULL) {
    log->Error("strategy_pool_worker_ready_timeout id=" + std::to_string(w->id) +
               " sock=" + w->sock_path);
  }
  return false;
}

void StrategyWorkerPool::KillWorker(Worker *w) {
  if (w == NULL) {
    return;
  }
  if (w->fd >= 0) {
    ::close(w->fd);
    w->fd = -1;
  }
  if (w->pid > 0) {
    ::kill(w->pid, SIGTERM);
    for (int i = 0; i < 50; ++i) {
      int st = 0;
      const pid_t rp = ::waitpid(w->pid, &st, WNOHANG);
      if (rp == w->pid || (rp < 0 && errno == ECHILD)) {
        break;
      }
      usleep(20000);
    }
    ::kill(w->pid, SIGKILL);
    int st = 0;
    (void)::waitpid(w->pid, &st, 0);
    w->pid = -1;
  }
  if (!w->sock_path.empty()) {
    ::unlink(w->sock_path.c_str());
  }
  w->alive = false;
  w->busy = false;
}

bool StrategyWorkerPool::EnsureWorker(Worker *w, SystemLogger *log) {
  if (w == NULL) {
    return false;
  }
  if (w->alive && w->fd >= 0 && w->pid > 0) {
    int st = 0;
    const pid_t rp = ::waitpid(w->pid, &st, WNOHANG);
    if (rp == 0) {
      return true;
    }
  }
  return SpawnOne(w->id, log);
}

StrategyPoolRunResult StrategyWorkerPool::RunOnWorker(Worker *w, const StrategyPoolRunRequest &req,
                                                      SystemLogger *log) {
  StrategyPoolRunResult out;
  out.used_pool = true;
  if (w == NULL) {
    out.error = "worker null";
    return out;
  }
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (!EnsureWorker(w, log)) {
      out.error = "worker unavailable";
      return out;
    }
  }
  nlohmann::json cmd = {{"cmd", "run"},
                        {"config", req.config_path},
                        {"latency_staging", req.latency_staging},
                        {"latency_csv", req.latency_csv},
                        {"run_log", req.run_log}};
  const std::string line = cmd.dump() + "\n";
  if (!WriteAll(w->fd, line)) {
    out.error = "write to worker failed";
    std::lock_guard<std::mutex> lk(mu_);
    KillWorker(w);
    return out;
  }
  const int timeout_ms = req.timeout_sec > 0 ? req.timeout_sec * 1000 : 0;
  std::string resp;
  if (!ReadLine(w->fd, &resp, timeout_ms > 0 ? timeout_ms : 0)) {
    out.error = "worker response timeout/eof";
    std::lock_guard<std::mutex> lk(mu_);
    KillWorker(w);
    return out;
  }
  try {
    const nlohmann::json j = nlohmann::json::parse(resp);
    out.exit_code = j.value("exit_code", 1);
    out.ok = j.value("ok", false) && out.exit_code == 0;
    out.error = j.value("error", "");
    if (j.contains("run_outcome") && j["run_outcome"].is_object()) {
      out.run_outcome = j["run_outcome"];
    }
  } catch (const std::exception &e) {
    out.error = std::string("bad worker json: ") + e.what();
    std::lock_guard<std::mutex> lk(mu_);
    KillWorker(w);
  }
  return out;
}

StrategyPoolRunResult StrategyWorkerPool::Run(const StrategyPoolRunRequest &req, SystemLogger *log) {
  StrategyPoolRunResult out;
  out.used_pool = true;
  Worker *chosen = NULL;
  {
    std::unique_lock<std::mutex> lk(mu_);
    if (!enabled_ || workers_.empty()) {
      out.used_pool = false;
      out.error = "pool disabled";
      return out;
    }
    cv_.wait(lk, [&]() {
      if (!enabled_) {
        return true;
      }
      for (size_t i = 0; i < workers_.size(); ++i) {
        if (!workers_[i].busy) {
          return true;
        }
      }
      return false;
    });
    if (!enabled_) {
      out.used_pool = false;
      out.error = "pool stopped";
      return out;
    }
    for (size_t n = 0; n < workers_.size(); ++n) {
      const size_t idx = (rr_ + n) % workers_.size();
      if (!workers_[idx].busy) {
        workers_[idx].busy = true;
        chosen = &workers_[idx];
        rr_ = (idx + 1) % workers_.size();
        break;
      }
    }
  }
  if (chosen == NULL) {
    out.error = "no idle worker";
    return out;
  }
  out = RunOnWorker(chosen, req, log);
  {
    std::lock_guard<std::mutex> lk(mu_);
    chosen->busy = false;
  }
  cv_.notify_one();
  return out;
}

}  // namespace task
}  // namespace mmtrade
