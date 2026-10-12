#include "FTAPIDemoBase.h"
#include "bench_jsonl_writer.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace {

double NowMs() {
  using clock = std::chrono::steady_clock;
  return std::chrono::duration<double, std::milli>(clock::now().time_since_epoch()).count();
}

std::string JsonEscape(const std::string &s) {
  std::ostringstream oss;
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (c == '"')
      oss << "\\\"";
    else if (c == '\\')
      oss << "\\\\";
    else
      oss << c;
  }
  return oss.str();
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

std::vector<std::string> LoadSymbols(const std::string &path) {
  std::ifstream ifs(path.c_str());
  std::vector<std::string> out;
  std::string line;
  while (std::getline(ifs, line)) {
    if (line.empty() || line[0] == '#') continue;
    // trim
    size_t a = line.find_first_not_of(" \t\r\n");
    size_t b = line.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) continue;
    out.push_back(line.substr(a, b - a + 1));
  }
  return out;
}

}  // namespace

class PushBatchBench : public FTAPIDemoBase {
 public:
  PushBatchBench()
      : sub_ok_(false),
        push_count_(0),
        first_push_ms_(0.0),
        t_sub_start_(0.0),
        target_samples_(1) {}

  bool SubOrUnsub(const std::string &symbol, bool is_sub, int target_samples, double &ms_subscribe,
                  double &ms_first_push, int &push_count, std::vector<double> &intervals_ms,
                  double timeout_sec) {
    target_samples_ = target_samples <= 0 ? 1 : target_samples;
    push_count_ = 0;
    first_push_ms_ = 0.0;
    push_times_.clear();
    sub_ok_ = false;
    intervals_ms.clear();

    Qot_Common::QotMarket market = Qot_Common::QotMarket_US_Security;
    std::string code;
    ParseSymbol(symbol, market, code);

    Qot_Sub::Request req;
    Qot_Sub::C2S *c2s = req.mutable_c2s();
    Qot_Common::Security *sec = c2s->add_securitylist();
    sec->set_market(market);
    sec->set_code(code);
    c2s->add_subtypelist(Qot_Common::SubType_Basic);
    c2s->set_issuborunsub(is_sub);
    c2s->set_isregorunregpush(is_sub);
    c2s->set_extendedtime(true);

    t_sub_start_ = NowMs();
    WaitReply(m_pQotApi->Sub(req));
    ms_subscribe = NowMs() - t_sub_start_;
    if (!is_sub) {
      ms_first_push = 0.0;
      push_count = 0;
      return sub_ok_;
    }
    if (!sub_ok_) {
      ms_first_push = 0.0;
      push_count = 0;
      return false;
    }

    std::unique_lock<std::mutex> lk(mu_);
    // Phase 1: first push (up to min(15s, timeout))
    const double first_wait = timeout_sec < 15.0 ? timeout_sec : 15.0;
    cv_.wait_for(lk, std::chrono::milliseconds(static_cast<int>(first_wait * 1000.0)),
                 [&] { return first_push_ms_ > 0.0; });
    if (first_push_ms_ <= 0.0) {
      ms_first_push = 0.0;
      push_count = 0;
      return false;
    }
    // Phase 2: collect more for up to 8s
    const double collect_wait = 8.0;
    cv_.wait_for(lk, std::chrono::milliseconds(static_cast<int>(collect_wait * 1000.0)),
                 [&] { return push_count_ >= target_samples_; });
    ms_first_push = first_push_ms_;
    push_count = push_count_;
    for (size_t i = 1; i < push_times_.size(); ++i) {
      intervals_ms.push_back(push_times_[i] - push_times_[i - 1]);
    }
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
    const double now = NowMs();
    const int add = n > 0 ? n : 1;
    for (int i = 0; i < add; ++i) {
      push_times_.push_back(now);
    }
    push_count_ += add;
    if (first_push_ms_ <= 0.0) {
      first_push_ms_ = now - t_sub_start_;
    }
    cv_.notify_all();
  }

 private:
  bool sub_ok_;
  int push_count_;
  double first_push_ms_;
  double t_sub_start_;
  int target_samples_;
  std::vector<double> push_times_;
  std::mutex mu_;
  std::condition_variable cv_;
};

int main(int argc, char **argv) {
  std::string host = "127.0.0.1";
  int port = 11121;
  std::string symbols_file;
  std::string out_path;
  int samples = 20;
  double timeout_sec = 20.0;
  double gap_sec = 0.8;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--host" && i + 1 < argc)
      host = argv[++i];
    else if (a == "--port" && i + 1 < argc)
      port = std::atoi(argv[++i]);
    else if (a == "--symbols-file" && i + 1 < argc)
      symbols_file = argv[++i];
    else if (a == "--out" && i + 1 < argc)
      out_path = argv[++i];
    else if (a == "--samples" && i + 1 < argc)
      samples = std::atoi(argv[++i]);
    else if (a == "--timeout-sec" && i + 1 < argc)
      timeout_sec = std::atof(argv[++i]);
    else if (a == "--gap-sec" && i + 1 < argc)
      gap_sec = std::atof(argv[++i]);
  }
  if (out_path.empty() || symbols_file.empty()) {
    std::cerr << "usage: bench_push_batch --symbols-file list.txt --out path.jsonl "
                 "[--samples 20] [--timeout-sec 20] [--gap-sec 0.8]"
              << std::endl;
    return 2;
  }

  std::vector<std::string> symbols = LoadSymbols(symbols_file);
  if (symbols.empty()) {
    std::cerr << "empty symbols file" << std::endl;
    return 2;
  }

  BenchJsonlWriter jsonl_out(out_path);

  MMAPI::Init();
  PushBatchBench bench;
  double t_conn = NowMs();
  int rc = 0;
  if (!bench.InitQot(host.c_str(), static_cast<uint16_t>(port))) {
    std::cerr << "InitQot failed" << std::endl;
    rc = 1;
  } else {
    double ms_connect = NowMs() - t_conn;
    for (size_t i = 0; i < symbols.size(); ++i) {
      const std::string &symbol = symbols[i];
      double ms_subscribe = 0.0;
      double ms_first_push = 0.0;
      int push_count = 0;
      std::vector<double> intervals;
      bool ok = false;
      for (int attempt = 1; attempt <= 2 && !ok; ++attempt) {
        ok = bench.SubOrUnsub(symbol, true, samples, ms_subscribe, ms_first_push, push_count,
                              intervals, timeout_sec);
        if (!ok) {
          std::cerr << "[cpp warm] FAIL " << symbol << " attempt=" << attempt << std::endl;
        }
      }
      // unsubscribe to free quota
      double ms_unsub = 0.0, dummy_fp = 0.0;
      int dummy_pc = 0;
      std::vector<double> dummy_iv;
      bench.SubOrUnsub(symbol, false, 1, ms_unsub, dummy_fp, dummy_pc, dummy_iv, 5.0);

      std::ostringstream oss;
      oss << std::fixed << std::setprecision(3);
      oss << "{\"case\":\"push_basicqot_warm\",\"path\":\"warm\",\"lang\":\"cpp\",\"ok\":"
          << (ok ? "true" : "false") << ",\"ms_connect\":" << ms_connect
          << ",\"ms_subscribe\":" << ms_subscribe << ",\"ms_first_push\":" << ms_first_push
          << ",\"push_count\":" << push_count << ",\"symbol\":\"" << JsonEscape(symbol) << "\""
          << ",\"timeout_sec\":" << timeout_sec << ",\"target_samples\":" << samples
          << ",\"intervals_ms\":[";
      for (size_t k = 0; k < intervals.size(); ++k) {
        if (k) oss << ",";
        oss << intervals[k];
      }
      oss << "]}";
      jsonl_out.AppendLine(oss.str());
      std::cerr << "[cpp warm] " << (i + 1) << "/" << symbols.size() << " " << symbol
                << " ok=" << ok << " first_push_ms=" << ms_first_push
                << " pushes=" << push_count << std::endl;
      if (!ok) rc = 1;
      if (gap_sec > 0) {
        timespec ts;
        ts.tv_sec = static_cast<time_t>(gap_sec);
        ts.tv_nsec = static_cast<long>((gap_sec - ts.tv_sec) * 1e9);
        nanosleep(&ts, nullptr);
      }
    }
    bench.UnInitQot();
  }
  protobuf::ShutdownProtobufLibrary();
  MMAPI::UnInit();
  return rc;
}
