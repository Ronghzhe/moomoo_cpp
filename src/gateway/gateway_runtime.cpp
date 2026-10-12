#include "gateway/gateway_runtime.hpp"

#include "opend/order_client.hpp"
#include "opend/order_config.hpp"
#include "trade/order_errors.hpp"
#include "trade/order_id_util.hpp"
#include "trade/order_parse.hpp"
#include "trade/trade_pwd_util.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>

namespace mmtrade {
namespace gateway {

namespace {

double SteadyNowSec() {
  using clock = std::chrono::steady_clock;
  return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

std::string UtcNowIso() {
  const std::time_t t = std::time(NULL);
  std::tm tm_utc;
#if defined(_WIN32)
  gmtime_s(&tm_utc, &t);
#else
  gmtime_r(&t, &tm_utc);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
  return std::string(buf);
}

std::string JsonString(const nlohmann::json &j, const char *key, const std::string &fallback = "") {
  if (!j.contains(key) || j[key].is_null()) {
    return fallback;
  }
  if (j[key].is_string()) {
    return j[key].get<std::string>();
  }
  if (j[key].is_number_integer()) {
    return std::to_string(j[key].get<long long>());
  }
  if (j[key].is_number()) {
    std::ostringstream oss;
    oss << j[key].get<double>();
    return oss.str();
  }
  return fallback;
}

int JsonInt(const nlohmann::json &j, const char *key, int fallback) {
  if (!j.contains(key) || j[key].is_null()) {
    return fallback;
  }
  if (j[key].is_number_integer()) {
    return j[key].get<int>();
  }
  if (j[key].is_string()) {
    return std::atoi(j[key].get<std::string>().c_str());
  }
  return fallback;
}

double JsonDouble(const nlohmann::json &j, const char *key, double fallback) {
  if (!j.contains(key) || j[key].is_null()) {
    return fallback;
  }
  if (j[key].is_number()) {
    return j[key].get<double>();
  }
  if (j[key].is_string()) {
    return std::atof(j[key].get<std::string>().c_str());
  }
  return fallback;
}

bool JsonBool(const nlohmann::json &j, const char *key, bool fallback) {
  if (!j.contains(key) || j[key].is_null()) {
    return fallback;
  }
  if (j[key].is_boolean()) {
    return j[key].get<bool>();
  }
  return fallback;
}

std::string UpperCopy(std::string s) {
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] >= 'a' && s[i] <= 'z') {
      s[i] = static_cast<char>(s[i] - 'a' + 'A');
    }
  }
  return s;
}

trade::TrdMarket ParseMarketLabel(const std::string &raw) {
  const std::string m = UpperCopy(raw);
  if (m == "HK") {
    return trade::TrdMarket::kHK;
  }
  if (m == "CN" || m == "SH" || m == "SZ") {
    return trade::TrdMarket::kCN;
  }
  if (m == "SG") {
    return trade::TrdMarket::kSG;
  }
  return trade::TrdMarket::kUS;
}

common::MoomooRateConfig MakeRateConfig(const GatewayConfig &cfg) {
  common::MoomooRateConfig out;
  out.window_sec = cfg.moomoo_window_sec;
  out.max_requests = cfg.moomoo_max_per_window;
  out.min_interval_sec = cfg.moomoo_min_interval_sec;
  out.reserve_slots = cfg.moomoo_reserve_slots;
  return out;
}

}  // namespace

GatewayRuntime::GatewayRuntime(const GatewayConfig &cfg)
    : cfg_(cfg),
      scheduler_(cfg.queue_maxsize),
      place_limiter_(MakeRateConfig(cfg)),
      running_(true),
      accepted_total_(0),
      ok_total_(0),
      error_total_(0),
      freq_error_total_(0),
      rate_wait_total_sec_(0),
      avg_queue_wait_ms_(0),
      avg_exec_ms_(0) {
  if (!cfg_.log_dir.empty()) {
    event_log_path_ = cfg_.log_dir;
    if (!event_log_path_.empty() && event_log_path_[event_log_path_.size() - 1] != '/') {
      event_log_path_ += "/";
    }
    event_log_path_ += "gateway_events.jsonl";
  }
  worker_ = std::thread(&GatewayRuntime::WorkerLoop, this);
}

GatewayRuntime::~GatewayRuntime() { Close(); }

void GatewayRuntime::Close() {
  if (!running_.exchange(false)) {
    return;
  }
  if (worker_.joinable()) {
    worker_.join();
  }
  std::lock_guard<std::mutex> lock(client_mu_);
  for (std::map<std::string, std::unique_ptr<opend::OrderClient> >::iterator it =
           client_pool_.begin();
       it != client_pool_.end(); ++it) {
    if (it->second.get() != NULL) {
      it->second->Close();
    }
  }
  client_pool_.clear();
}

void GatewayRuntime::LogEvent(const nlohmann::json &event) {
  if (event_log_path_.empty()) {
    return;
  }
  nlohmann::json row = event;
  row["ts"] = UtcNowIso();
  const std::string line = row.dump() + "\n";
  std::lock_guard<std::mutex> lock(event_log_mu_);
  std::ofstream ofs(event_log_path_.c_str(), std::ios::app);
  if (ofs) {
    ofs << line;
  }
}

nlohmann::json GatewayRuntime::CacheGet(const std::string &request_id) {
  const double now = SteadyNowSec();
  std::lock_guard<std::mutex> lock(done_mu_);
  std::unordered_map<std::string, std::pair<double, nlohmann::json> >::iterator it =
      done_cache_.find(request_id);
  if (it == done_cache_.end()) {
    return nlohmann::json();
  }
  if (now - it->second.first > 300.0) {
    done_cache_.erase(it);
    return nlohmann::json();
  }
  return it->second.second;
}

void GatewayRuntime::CachePut(const std::string &request_id, const nlohmann::json &result) {
  std::lock_guard<std::mutex> lock(done_mu_);
  done_cache_[request_id] = std::make_pair(SteadyNowSec(), result);
}

std::string GatewayRuntime::ResolveAccId(const nlohmann::json &payload) const {
  nlohmann::json ctx = nlohmann::json::object();
  if (payload.contains("context") && payload["context"].is_object()) {
    ctx = payload["context"];
  }
  const std::string raw = JsonString(ctx, "acc_id");
  if (!raw.empty()) {
    return raw;
  }
  const std::string account_key = JsonString(ctx, "account_key");
  if (account_key.size() > 4 && account_key.compare(0, 4, "acc:") == 0) {
    return account_key.substr(4);
  }
  return std::to_string(static_cast<unsigned long long>(cfg_.acc_id));
}

void GatewayRuntime::ResolveRoute(const nlohmann::json &payload, std::string *host, int *port,
                                  std::string *trd_env, std::string *trd_market,
                                  std::uint64_t *acc_id, int *acc_index) const {
  nlohmann::json ctx = nlohmann::json::object();
  if (payload.contains("context") && payload["context"].is_object()) {
    ctx = payload["context"];
  }
  *host = JsonString(ctx, "opend_host", cfg_.opend_host);
  if (host->empty()) {
    *host = "127.0.0.1";
  }
  *port = JsonInt(ctx, "opend_port", cfg_.opend_port);
  if (*port <= 0) {
    *port = 11121;
  }
  *trd_env = UpperCopy(JsonString(ctx, "trd_env", cfg_.trd_env));
  if (trd_env->empty()) {
    *trd_env = "SIMULATE";
  }
  *trd_market = JsonString(ctx, "trd_market", cfg_.trd_market);
  if (trd_market->empty()) {
    *trd_market = "US";
  }
  *acc_id = static_cast<std::uint64_t>(JsonInt(ctx, "acc_id", static_cast<int>(cfg_.acc_id)));
  if (ctx.contains("acc_id") && ctx["acc_id"].is_string()) {
    *acc_id = static_cast<std::uint64_t>(std::strtoull(ctx["acc_id"].get<std::string>().c_str(),
                                                      NULL, 10));
  }
  *acc_index = JsonInt(ctx, "acc_index", cfg_.acc_index);
}

opend::OrderClient *GatewayRuntime::GetClient(const nlohmann::json &payload) {
  std::string host;
  int port = 0;
  std::string trd_env;
  std::string trd_market;
  std::uint64_t acc_id = 0;
  int acc_index = 0;
  ResolveRoute(payload, &host, &port, &trd_env, &trd_market, &acc_id, &acc_index);
  std::ostringstream key;
  key << host << ":" << port << ":" << trd_env << ":" << trd_market << ":" << acc_id << ":"
      << acc_index;
  std::lock_guard<std::mutex> lock(client_mu_);
  std::map<std::string, std::unique_ptr<opend::OrderClient> >::iterator it =
      client_pool_.find(key.str());
  if (it != client_pool_.end()) {
    return it->second.get();
  }
  opend::OrderConfig oc;
  oc.host = host;
  oc.port = port;
  oc.simulate = (trd_env != "REAL");
  oc.trd_market = ParseMarketLabel(trd_market);
  oc.acc_id = acc_id;
  oc.refresh_cache = true;
  std::unique_ptr<opend::OrderClient> client(new opend::OrderClient(oc));
  if (!client->Connect()) {
    return NULL;
  }
  opend::OrderClient *raw = client.get();
  client_pool_[key.str()] = std::move(client);
  return raw;
}

void GatewayRuntime::SetResult(const std::string &request_id, const nlohmann::json &result) {
  std::shared_ptr<Pending> p;
  {
    std::lock_guard<std::mutex> lock(pending_mu_);
    std::unordered_map<std::string, std::shared_ptr<Pending> >::iterator it =
        pending_.find(request_id);
    if (it != pending_.end()) {
      p = it->second;
      pending_.erase(it);
    }
  }
  CachePut(request_id, result);
  if (p.get() == NULL) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(p->mu);
    p->result = result;
    p->done = true;
  }
  p->cv.notify_all();
  nlohmann::json ev = {{"kind", "result_set"},
                       {"request_id", request_id},
                       {"ok", result.value("ok", false)},
                       {"ret_code", result.value("ret_code", -1)},
                       {"status", result.value("status", "")},
                       {"error", result.value("error", "")}};
  if (result.contains("queue_wait_ms")) {
    ev["queue_wait_ms"] = result["queue_wait_ms"];
  }
  if (result.contains("exec_ms")) {
    ev["exec_ms"] = result["exec_ms"];
  }
  if (result.contains("rate_wait_ms")) {
    ev["rate_wait_ms"] = result["rate_wait_ms"];
  }
  if (result.contains("rate_wait_actual_ms")) {
    ev["rate_wait_actual_ms"] = result["rate_wait_actual_ms"];
  }
  if (result.contains("client_get_ms")) {
    ev["client_get_ms"] = result["client_get_ms"];
  }
  if (result.contains("place_wall_ms")) {
    ev["place_wall_ms"] = result["place_wall_ms"];
  }
  if (result.contains("place_pre_us")) {
    ev["place_pre_us"] = result["place_pre_us"];
  }
  if (result.contains("place_sdk_us")) {
    ev["place_sdk_us"] = result["place_sdk_us"];
  }
  if (result.contains("place_post_us")) {
    ev["place_post_us"] = result["place_post_us"];
  }
  LogEvent(ev);
}

nlohmann::json GatewayRuntime::Submit(const nlohmann::json &payload) {
  const std::string rid = JsonString(payload, "request_id");
  if (rid.empty()) {
    return nlohmann::json{{"ok", false}, {"ret_code", -1}, {"error", "request_id required"}};
  }
  nlohmann::json cached = CacheGet(rid);
  if (!cached.is_null() && cached.is_object() && !cached.empty()) {
    cached["dedup"] = true;
    LogEvent({{"kind", "submit_dedup"}, {"request_id", rid}, {"ok", cached.value("ok", false)}});
    return cached;
  }

  std::shared_ptr<Pending> p;
  bool enqueued = false;
  int priority = 50;
  {
    std::lock_guard<std::mutex> lock(pending_mu_);
    std::unordered_map<std::string, std::shared_ptr<Pending> >::iterator it = pending_.find(rid);
    if (it != pending_.end()) {
      p = it->second;
    } else {
      p.reset(new Pending());
      p->payload = payload;
      p->enqueued_mono = SteadyNowSec();
      pending_[rid] = p;
      nlohmann::json ctx = nlohmann::json::object();
      if (payload.contains("context") && payload["context"].is_object()) {
        ctx = payload["context"];
      }
      priority = JsonInt(ctx, "priority", 50);
      if (!scheduler_.TryPut(rid, priority)) {
        pending_.erase(rid);
        return nlohmann::json{{"ok", false},
                              {"ret_code", -1},
                              {"error", "gateway queue full"},
                              {"request_id", rid}};
      }
      enqueued = true;
      {
        std::lock_guard<std::mutex> slock(stats_mu_);
        accepted_total_ += 1.0;
      }
      LogEvent({{"kind", "submit_accepted"},
                {"request_id", rid},
                {"priority", priority},
                {"queue_size_after", static_cast<int>(scheduler_.size())}});
    }
  }

  std::unique_lock<std::mutex> lock(p->mu);
  const bool done =
      p->cv.wait_for(lock, std::chrono::duration<double>(cfg_.request_timeout_sec),
                     [&p]() { return p->done; });
  if (!done) {
    LogEvent({{"kind", "submit_timeout"},
              {"request_id", rid},
              {"timeout_sec", cfg_.request_timeout_sec}});
    return nlohmann::json{{"ok", false},
                          {"ret_code", -1},
                          {"error", "gateway timeout waiting result"},
                          {"request_id", rid}};
  }
  (void)enqueued;
  return p->result.is_null()
             ? nlohmann::json{{"ok", false}, {"ret_code", -1}, {"error", "missing result"}}
             : p->result;
}

void GatewayRuntime::WorkerLoop() {
  while (running_.load()) {
    ScheduledRequest item;
    if (!scheduler_.Get(&item, 0.3)) {
      continue;
    }
    std::shared_ptr<Pending> pending;
    {
      std::lock_guard<std::mutex> lock(pending_mu_);
      std::unordered_map<std::string, std::shared_ptr<Pending> >::iterator it =
          pending_.find(item.request_id);
      if (it == pending_.end()) {
        continue;
      }
      pending = it->second;
    }
    if (pending.get() == NULL) {
      continue;
    }
    const std::string acc_id = ResolveAccId(pending->payload);
    const double t_rate0 = SteadyNowSec();
    const double rate_wait =
        place_limiter_.WaitUntilAllowed(acc_id, cfg_.moomoo_pre_place_max_wait_sec);
    {
      std::lock_guard<std::mutex> lock(stats_mu_);
      rate_wait_total_sec_ += rate_wait;
    }
    const double t0 = SteadyNowSec();
    nlohmann::json result =
        ExecutePlace(pending->payload, pending->enqueued_mono, t0, acc_id);
    result["rate_wait_ms"] = rate_wait * 1000.0;
    result["rate_wait_actual_ms"] = (t0 - t_rate0) * 1000.0;
    SetResult(item.request_id, result);
  }
}

nlohmann::json GatewayRuntime::ExecutePlace(const nlohmann::json &payload, double enqueued_mono,
                                            double start_mono, const std::string &acc_id) {
  const std::string req_id = JsonString(payload, "request_id");
  nlohmann::json ctx = nlohmann::json::object();
  if (payload.contains("context") && payload["context"].is_object()) {
    ctx = payload["context"];
  }

  if (cfg_.dry_run) {
    const double queue_wait_ms = (start_mono - enqueued_mono) * 1000.0;
    const double exec_ms = (SteadyNowSec() - start_mono) * 1000.0;
    {
      std::lock_guard<std::mutex> lock(stats_mu_);
      ok_total_ += 1.0;
      avg_queue_wait_ms_ = avg_queue_wait_ms_ * 0.9 + queue_wait_ms * 0.1;
      avg_exec_ms_ = avg_exec_ms_ * 0.9 + exec_ms * 0.1;
    }
    const std::string suffix =
        req_id.size() > 8 ? req_id.substr(req_id.size() - 8) : req_id;
    return nlohmann::json{{"ok", true},
                          {"ret_code", 0},
                          {"order_id", std::string("DRYRUN-") + suffix},
                          {"status", "DRYRUN_ACCEPTED"},
                          {"error", ""},
                          {"request_id", req_id},
                          {"queue_wait_ms", queue_wait_ms},
                          {"exec_ms", exec_ms},
                          {"strategy_id", JsonString(ctx, "strategy_id")},
                          {"symbol", JsonString(ctx, "symbol")},
                          {"acc_id", acc_id}};
  }

  try {
    const double t_client0 = SteadyNowSec();
    opend::OrderClient *client = GetClient(payload);
    const double client_get_ms = (SteadyNowSec() - t_client0) * 1000.0;
    if (client == NULL) {
      std::lock_guard<std::mutex> lock(stats_mu_);
      error_total_ += 1.0;
      return nlohmann::json{{"ok", false},
                            {"ret_code", -1},
                            {"order_id", ""},
                            {"status", ""},
                            {"error", "gateway execute exception: connect failed"},
                            {"request_id", req_id},
                            {"acc_id", acc_id},
                            {"client_get_ms", client_get_ms}};
    }

    std::string host;
    int port = 0;
    std::string trd_env;
    std::string trd_market;
    std::uint64_t route_acc = 0;
    int acc_index = 0;
    ResolveRoute(payload, &host, &port, &trd_env, &trd_market, &route_acc, &acc_index);
    (void)host;
    (void)port;
    (void)trd_market;
    (void)route_acc;
    (void)acc_index;
    if (trd_env == "REAL") {
      // Unlock is handled inside OrderClient::Connect via trade password md5.
    }

    const double t_place0 = SteadyNowSec();
    const opend::PlaceOrderResult raw = client->Place(
        JsonString(payload, "code"), JsonString(payload, "side"), JsonDouble(payload, "qty", 0.0),
        JsonDouble(payload, "price", 0.0), JsonString(payload, "order_type", "NORMAL"),
        JsonString(payload, "remark"), JsonString(payload, "time_in_force", "DAY"),
        JsonBool(payload, "fill_outside_rth", false));
    const double place_wall_ms = (SteadyNowSec() - t_place0) * 1000.0;

    const double queue_wait_ms = (start_mono - enqueued_mono) * 1000.0;
    const double exec_ms = (SteadyNowSec() - start_mono) * 1000.0;
    const std::string oid = trade::ResolveDisplayOrderId(raw.order_id, raw.order_id_ex);
    std::string err = raw.error;
    if (!raw.ok && err.empty()) {
      err = "place failed";
    }
    if (!raw.ok && (trade::IsOrderRateLimitErr(err) || trade::IsOperatingTooFastErr(err))) {
      std::lock_guard<std::mutex> lock(stats_mu_);
      freq_error_total_ += 1.0;
      place_limiter_.OnFrequencyError(acc_id, cfg_.moomoo_freq_backoff_sec);
    }
    {
      std::lock_guard<std::mutex> lock(stats_mu_);
      if (raw.ok) {
        ok_total_ += 1.0;
      } else {
        error_total_ += 1.0;
      }
      avg_queue_wait_ms_ = avg_queue_wait_ms_ * 0.9 + queue_wait_ms * 0.1;
      avg_exec_ms_ = avg_exec_ms_ * 0.9 + exec_ms * 0.1;
    }
    return nlohmann::json{{"ok", raw.ok},
                          {"ret_code", raw.ret_code},
                          {"order_id", oid},
                          {"status", raw.ok ? "SUBMITTING" : ""},
                          {"error", err},
                          {"request_id", req_id},
                          {"queue_wait_ms", queue_wait_ms},
                          {"exec_ms", exec_ms},
                          {"client_get_ms", client_get_ms},
                          {"place_wall_ms", place_wall_ms},
                          {"place_pre_us", raw.pre_sdk_us},
                          {"place_sdk_us", raw.sdk_us},
                          {"place_post_us", raw.post_sdk_us},
                          {"strategy_id", JsonString(ctx, "strategy_id")},
                          {"symbol", JsonString(ctx, "symbol")},
                          {"acc_id", acc_id}};
  } catch (const std::exception &e) {
    std::lock_guard<std::mutex> lock(stats_mu_);
    error_total_ += 1.0;
    return nlohmann::json{{"ok", false},
                          {"ret_code", -1},
                          {"order_id", ""},
                          {"status", ""},
                          {"error", std::string("gateway execute exception: ") + e.what()},
                          {"request_id", req_id},
                          {"acc_id", acc_id}};
  } catch (...) {
    std::lock_guard<std::mutex> lock(stats_mu_);
    error_total_ += 1.0;
    return nlohmann::json{{"ok", false},
                          {"ret_code", -1},
                          {"order_id", ""},
                          {"status", ""},
                          {"error", "gateway execute exception: unknown"},
                          {"request_id", req_id},
                          {"acc_id", acc_id}};
  }
}

nlohmann::json GatewayRuntime::Metrics() const {
  std::lock_guard<std::mutex> lock(stats_mu_);
  nlohmann::json out = {{"accepted_total", accepted_total_},
                        {"ok_total", ok_total_},
                        {"error_total", error_total_},
                        {"freq_error_total", freq_error_total_},
                        {"rate_wait_total_sec", rate_wait_total_sec_},
                        {"avg_queue_wait_ms", avg_queue_wait_ms_},
                        {"avg_exec_ms", avg_exec_ms_},
                        {"queue_size", static_cast<int>(scheduler_.size())}};
  out["moomoo_rate"] = {{"window_sec", cfg_.moomoo_window_sec},
                        {"max_per_window", cfg_.moomoo_max_per_window},
                        {"effective_max_per_window", place_limiter_.EffectiveMax()},
                        {"min_interval_sec", cfg_.moomoo_min_interval_sec},
                        {"reserve_slots", cfg_.moomoo_reserve_slots}};
  const common::PlaceLimiterSnapshot snap =
      place_limiter_.Snapshot(std::to_string(static_cast<unsigned long long>(cfg_.acc_id)),
                              SteadyNowSec());
  out["moomoo_rate_acc"] = {{"used_in_window", snap.used_in_window},
                            {"max_in_window", snap.max_in_window},
                            {"window_sec", snap.window_sec},
                            {"min_interval_sec", snap.min_interval_sec},
                            {"wait_sec", snap.wait_sec}};
  return out;
}

}  // namespace gateway
}  // namespace mmtrade
