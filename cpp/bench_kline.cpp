#include "FTAPIDemoBase.h"
#include "bench_jsonl_writer.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace {

double NowMs() {
  using clock = std::chrono::steady_clock;
  return std::chrono::duration<double, std::milli>(clock::now().time_since_epoch()).count();
}

class Ticker {
 public:
  Ticker() : t0_(NowMs()) {}
  double ElapsedMs() const { return NowMs() - t0_; }
  void Reset() { t0_ = NowMs(); }

 private:
  double t0_;
};

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

void ParseSymbol(const std::string &sym, Qot_Common::QotMarket &market, std::string &code) {
  std::string s = sym;
  if (s.find("US.") == 0) {
    market = Qot_Common::QotMarket_US_Security;
    code = s.substr(3);
  } else if (s.find("HK.") == 0) {
    market = Qot_Common::QotMarket_HK_Security;
    code = s.substr(3);
  } else {
    market = Qot_Common::QotMarket_US_Security;
    code = s;
  }
}

}  // namespace

class KlineBench : public FTAPIDemoBase {
 public:
  KlineBench()
      : bar_count_(0),
        get_ok_(false),
        sub_ok_(false),
        market_(Qot_Common::QotMarket_US_Security) {}

  // warm：连接后调用一次；循环内只 FetchKl。
  bool Subscribe(const std::string &symbol, double *ms_sub_out) {
    sub_ok_ = false;
    ParseSymbol(symbol, market_, code_);

    Qot_Sub::Request sub_req;
    Qot_Sub::C2S *sub_c2s = sub_req.mutable_c2s();
    Qot_Common::Security *sec = sub_c2s->add_securitylist();
    sec->set_market(market_);
    sec->set_code(code_);
    sub_c2s->add_subtypelist(Qot_Common::SubType_KL_5Min);
    sub_c2s->set_issuborunsub(true);
    sub_c2s->set_isregorunregpush(false);
    sub_c2s->set_extendedtime(true);

    Ticker t;
    WaitReply(m_pQotApi->Sub(sub_req));
    if (ms_sub_out != NULL) {
      *ms_sub_out = t.ElapsedMs();
    }
    return sub_ok_;
  }

  bool FetchKl(int bars, double &ms_get_kl, int &count) {
    bar_count_ = 0;
    get_ok_ = false;

    Qot_GetKL::Request get_req;
    Qot_GetKL::C2S *get_c2s = get_req.mutable_c2s();
    get_c2s->set_rehabtype(Qot_Common::RehabType_Forward);
    get_c2s->set_kltype(Qot_Common::KLType_5Min);
    Qot_Common::Security *gsec = get_c2s->mutable_security();
    gsec->set_market(market_);
    gsec->set_code(code_);
    get_c2s->set_reqnum(bars);

    Ticker t;
    WaitReply(m_pQotApi->GetKL(get_req));
    ms_get_kl = t.ElapsedMs();
    count = bar_count_;
    return get_ok_;
  }

  // cold：每轮新连接后 Sub + GetKL；ms_fetch = 两者之和（与旧 bench 语义一致）。
  bool FetchOnce(const std::string &symbol, int bars, double &ms_fetch, int &count) {
    double ms_sub = 0.0;
    if (!Subscribe(symbol, &ms_sub)) {
      ms_fetch = ms_sub;
      count = 0;
      return false;
    }
    double ms_get = 0.0;
    const bool ok = FetchKl(bars, ms_get, count);
    ms_fetch = ms_sub + ms_get;
    return ok;
  }

  void OnReply_Sub(Moomoo::u32_t nSerialNo, const Qot_Sub::Response &stRsp) override {
    sub_ok_ = (stRsp.rettype() == Common::RetType_Succeed);
    if (!sub_ok_) {
      PrintUTF8(stRsp.retmsg());
    }
    PostReply();
  }

  void OnReply_GetKL(Moomoo::u32_t nSerialNo, const Qot_GetKL::Response &stRsp) override {
    get_ok_ = (stRsp.rettype() == Common::RetType_Succeed);
    if (get_ok_) {
      bar_count_ = stRsp.s2c().kllist_size();
    } else {
      PrintUTF8(stRsp.retmsg());
    }
    PostReply();
  }

 private:
  int bar_count_;
  bool get_ok_;
  bool sub_ok_;
  Qot_Common::QotMarket market_;
  std::string code_;
};

int main(int argc, char **argv) {
  std::string host = "127.0.0.1";
  int port = 11121;
  std::string symbol = "US.AAPL";
  int bars = 80;
  int warm_iters = 50;
  int cold_iters = 5;
  std::string mode = "both";
  std::string out_path;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--host" && i + 1 < argc) host = argv[++i];
    else if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
    else if (a == "--symbol" && i + 1 < argc) symbol = argv[++i];
    else if (a == "--bars" && i + 1 < argc) bars = std::atoi(argv[++i]);
    else if (a == "--warm-iters" && i + 1 < argc) warm_iters = std::atoi(argv[++i]);
    else if (a == "--cold-iters" && i + 1 < argc) cold_iters = std::atoi(argv[++i]);
    else if (a == "--mode" && i + 1 < argc) mode = argv[++i];
    else if (a == "--out" && i + 1 < argc) out_path = argv[++i];
  }
  if (out_path.empty()) {
    std::cerr << "usage: bench_kline --out path.jsonl [--mode warm|cold|both]" << std::endl;
    return 2;
  }

  BenchJsonlWriter jsonl_out(out_path);

  MMAPI::Init();
  int rc = 0;

  if (mode == "warm" || mode == "both") {
    KlineBench bench;
    Ticker t_conn;
    if (!bench.InitQot(host.c_str(), static_cast<uint16_t>(port))) {
      std::cerr << "InitQot failed" << std::endl;
      rc = 1;
    } else {
      double ms_connect = t_conn.ElapsedMs();
      double ms_sub = 0.0;
      if (!bench.Subscribe(symbol, &ms_sub)) {
        std::cerr << "warm Subscribe failed" << std::endl;
        rc = 1;
      } else {
        for (int i = 1; i <= warm_iters; ++i) {
          double ms_fetch = 0.0;
          int count = 0;
          const bool ok = bench.FetchKl(bars, ms_fetch, count);
          std::ostringstream oss;
          oss << std::fixed << std::setprecision(3);
          oss << "{\"case\":\"kline_warm\",\"lang\":\"cpp\",\"iter\":" << i
              << ",\"ok\":" << (ok ? "true" : "false") << ",\"ms_total\":" << ms_fetch
              << ",\"ms_connect\":" << (i == 1 ? ms_connect : 0.0)
              << ",\"ms_sub\":" << (i == 1 ? ms_sub : 0.0) << ",\"ms_fetch\":" << ms_fetch
              << ",\"symbol\":\"" << JsonEscape(symbol) << "\",\"kl_type\":\"K_5M\",\"bars\":" << bars
              << ",\"kline_src\":\"get_kl\",\"bar_count\":" << count
              << ",\"trd_env\":\"SIMULATE\"}";
          jsonl_out.AppendLine(oss.str());
          if (!ok) rc = 1;
        }
      }
      bench.UnInitQot();
    }
  }

  if (mode == "cold" || mode == "both") {
    for (int i = 1; i <= cold_iters; ++i) {
      KlineBench bench;
      Ticker t_all;
      if (!bench.InitQot(host.c_str(), static_cast<uint16_t>(port))) {
        std::cerr << "cold InitQot failed iter=" << i << std::endl;
        rc = 1;
        continue;
      }
      double ms_connect = t_all.ElapsedMs();
      double ms_fetch = 0.0;
      int count = 0;
      bool ok = bench.FetchOnce(symbol, bars, ms_fetch, count);
      double ms_total = t_all.ElapsedMs();
      bench.UnInitQot();
      std::ostringstream oss;
      oss << std::fixed << std::setprecision(3);
      oss << "{\"case\":\"kline_cold\",\"lang\":\"cpp\",\"iter\":" << i
          << ",\"ok\":" << (ok ? "true" : "false") << ",\"ms_total\":" << ms_total
          << ",\"ms_connect\":" << ms_connect << ",\"ms_fetch\":" << ms_fetch
          << ",\"symbol\":\"" << JsonEscape(symbol) << "\",\"kl_type\":\"K_5M\",\"bars\":" << bars
          << ",\"kline_src\":\"get_kl\",\"bar_count\":" << count
          << ",\"trd_env\":\"SIMULATE\"}";
      jsonl_out.AppendLine(oss.str());
      if (!ok) rc = 1;
    }
  }

  protobuf::ShutdownProtobufLibrary();
  MMAPI::UnInit();
  return rc;
}
