#include "FTAPIDemoBase.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

namespace {

double NowMs() {
  using clock = std::chrono::steady_clock;
  return std::chrono::duration<double, std::milli>(clock::now().time_since_epoch()).count();
}

std::string JsonEscape(const std::string &s) {
  std::ostringstream oss;
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (c == '"') oss << "\\\"";
    else if (c == '\\') oss << "\\\\";
    else oss << c;
  }
  return oss.str();
}

void AppendJsonl(const std::string &path, const std::string &line) {
  std::ofstream ofs(path.c_str(), std::ios::app);
  if (!ofs.good()) {
    std::cerr << "failed to open " << path << std::endl;
    return;
  }
  ofs << line << "\n";
}

void ParseSymbol(const std::string &sym, Qot_Common::QotMarket &market, std::string &code) {
  if (sym.find("US.") == 0) {
    market = Qot_Common::QotMarket_US_Security;
    code = sym.substr(3);
  } else if (sym.find("HK.") == 0) {
    market = Qot_Common::QotMarket_HK_Security;
    code = sym.substr(3);
  } else {
    market = Qot_Common::QotMarket_US_Security;
    code = sym;
  }
}

}  // namespace

class PushBench : public FTAPIDemoBase {
 public:
  PushBench() : sub_ok_(false), push_count_(0), first_push_ms_(0.0), t_sub_start_(0.0), target_samples_(1) {}

  bool SubscribeBasic(const std::string &symbol, int target_samples, double &ms_subscribe, double &ms_first_push,
                      int &push_count, double timeout_sec) {
    target_samples_ = target_samples <= 0 ? 1 : target_samples;
    push_count_ = 0;
    first_push_ms_ = 0.0;
    sub_ok_ = false;

    Qot_Common::QotMarket market = Qot_Common::QotMarket_US_Security;
    std::string code;
    ParseSymbol(symbol, market, code);

    Qot_Sub::Request req;
    Qot_Sub::C2S *c2s = req.mutable_c2s();
    Qot_Common::Security *sec = c2s->add_securitylist();
    sec->set_market(market);
    sec->set_code(code);
    c2s->add_subtypelist(Qot_Common::SubType_Basic);
    c2s->set_issuborunsub(true);
    c2s->set_isregorunregpush(true);
    c2s->set_extendedtime(true);

    t_sub_start_ = NowMs();
    WaitReply(m_pQotApi->Sub(req));
    ms_subscribe = NowMs() - t_sub_start_;
    if (!sub_ok_) {
      ms_first_push = 0.0;
      push_count = 0;
      return false;
    }

    std::unique_lock<std::mutex> lk(mu_);
    cv_.wait_for(lk, std::chrono::milliseconds(static_cast<int>(timeout_sec * 1000.0)), [&] {
      return push_count_ >= target_samples_;
    });
    ms_first_push = first_push_ms_;
    push_count = push_count_;
    return first_push_ms_ > 0.0;
  }

  void OnReply_Sub(Moomoo::u32_t, const Qot_Sub::Response &stRsp) override {
    sub_ok_ = (stRsp.rettype() == Common::RetType_Succeed);
    if (!sub_ok_) {
      PrintUTF8(stRsp.retmsg());
    }
    PostReply();
  }

  void OnPush_UpdateBasicQot(const Qot_UpdateBasicQot::Response &stRsp) override {
    int n = 0;
    if (stRsp.has_s2c()) {
      n = stRsp.s2c().basicqotlist_size();
    }
    std::lock_guard<std::mutex> lk(mu_);
    push_count_ += n > 0 ? n : 1;
    if (first_push_ms_ <= 0.0) {
      first_push_ms_ = NowMs() - t_sub_start_;
    }
    cv_.notify_all();
  }

 private:
  bool sub_ok_;
  int push_count_;
  double first_push_ms_;
  double t_sub_start_;
  int target_samples_;
  std::mutex mu_;
  std::condition_variable cv_;
};

int main(int argc, char **argv) {
  std::string host = "127.0.0.1";
  int port = 11121;
  std::string symbol = "US.AAPL";
  std::string out_path;
  int samples = 1;
  double timeout_sec = 10.0;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--host" && i + 1 < argc) host = argv[++i];
    else if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
    else if (a == "--symbol" && i + 1 < argc) symbol = argv[++i];
    else if (a == "--out" && i + 1 < argc) out_path = argv[++i];
    else if (a == "--samples" && i + 1 < argc) samples = std::atoi(argv[++i]);
    else if (a == "--timeout-sec" && i + 1 < argc) timeout_sec = std::atof(argv[++i]);
  }
  if (out_path.empty()) {
    std::cerr << "usage: bench_push --out path.jsonl" << std::endl;
    return 2;
  }

  MMAPI::Init();
  PushBench bench;
  double t_conn = NowMs();
  int rc = 0;
  if (!bench.InitQot(host.c_str(), static_cast<uint16_t>(port))) {
    std::cerr << "InitQot failed" << std::endl;
    rc = 1;
  } else {
    double ms_connect = NowMs() - t_conn;
    double ms_subscribe = 0.0;
    double ms_first_push = 0.0;
    int push_count = 0;
    bool ok = bench.SubscribeBasic(symbol, samples, ms_subscribe, ms_first_push, push_count, timeout_sec);
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);
    oss << "{\"case\":\"push_basicqot\",\"lang\":\"cpp\",\"iter\":1"
        << ",\"ok\":" << (ok ? "true" : "false")
        << ",\"ms_total\":" << (ok ? ms_first_push : ms_subscribe)
        << ",\"ms_connect\":" << ms_connect
        << ",\"ms_subscribe\":" << ms_subscribe
        << ",\"ms_first_push\":" << ms_first_push
        << ",\"push_count\":" << push_count
        << ",\"symbol\":\"" << JsonEscape(symbol) << "\""
        << ",\"timeout_sec\":" << timeout_sec << "}";
    AppendJsonl(out_path, oss.str());
    if (!ok) rc = 1;
    bench.UnInitQot();
  }
  protobuf::ShutdownProtobufLibrary();
  MMAPI::UnInit();
  return rc;
}
