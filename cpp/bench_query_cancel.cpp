#include "FTAPIDemoBase.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace {

double NowMs() {
  using clock = std::chrono::steady_clock;
  return std::chrono::duration<double, std::milli>(clock::now().time_since_epoch()).count();
}

class Ticker {
 public:
  Ticker() : t0_(NowMs()) {}
  double ElapsedMs() const { return NowMs() - t0_; }

 private:
  double t0_;
};

void AppendJsonl(const std::string &path, const std::string &line) {
  std::ofstream ofs(path.c_str(), std::ios::app);
  if (!ofs.good()) {
    std::cerr << "failed to open " << path << std::endl;
    return;
  }
  ofs << line << "\n";
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

std::string ParseCode(const std::string &sym) {
  if (sym.find("US.") == 0) return sym.substr(3);
  if (sym.find("HK.") == 0) return sym.substr(3);
  return sym;
}

struct MarketInfo {
  Trd_Common::TrdMarket trd_market;
  Trd_Common::TrdSecMarket sec_market;
  const char *name;
};

MarketInfo ParseMarket(const std::string &sym) {
  if (sym.find("HK.") == 0) {
    return {Trd_Common::TrdMarket_HK, Trd_Common::TrdSecMarket_HK, "HK"};
  }
  return {Trd_Common::TrdMarket_US, Trd_Common::TrdSecMarket_US, "US"};
}

void SleepSec(double sec) {
  if (sec <= 0) return;
  std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(sec * 1000.0)));
}

}  // namespace

class QueryCancelBench : public FTAPIDemoBase {
 public:
  QueryCancelBench() : order_id_(0), place_ok_(false), query_ok_(false), modify_ok_(false) {}

  bool RunOnce(const std::string &code_full, double qty, double price, double api_gap_sec,
               double &ms_place, double &ms_query, double &ms_cancel, uint64_t &order_id,
               bool &ok_query, bool &ok_cancel) {
    order_id_ = 0;
    place_ok_ = false;
    query_ok_ = false;
    modify_ok_ = false;
    ok_query = false;
    ok_cancel = false;

    std::string code = ParseCode(code_full);
    MarketInfo market = ParseMarket(code_full);
    uint64_t acc_id = GetFirstAccID(market.trd_market, Trd_Common::TrdEnv_Simulate);
    if (acc_id == 0) {
      std::cerr << "no " << market.name << " simulate account" << std::endl;
      return false;
    }

    Trd_PlaceOrder::Request place_req;
    Trd_PlaceOrder::C2S *place_c2s = place_req.mutable_c2s();
    Trd_Common::TrdHeader *hdr = place_c2s->mutable_header();
    hdr->set_trdenv(Trd_Common::TrdEnv_Simulate);
    hdr->set_accid(acc_id);
    hdr->set_trdmarket(market.trd_market);
    place_c2s->set_trdside(Trd_Common::TrdSide_Buy);
    place_c2s->set_ordertype(Trd_Common::OrderType_Normal);
    place_c2s->set_code(code);
    place_c2s->set_qty(qty);
    place_c2s->set_price(price);
    place_c2s->set_secmarket(market.sec_market);
    place_c2s->set_remark("bench_qc_cpp");

    Ticker t_place;
    WaitReply(m_pTrdApi->PlaceOrder(place_req));
    ms_place = t_place.ElapsedMs();
    if (!place_ok_ || order_id_ == 0) return false;

    SleepSec(api_gap_sec);
    Ticker t_query;
    if (QueryOrder(acc_id, market, code, order_id_)) {
      ms_query = t_query.ElapsedMs();
      ok_query = query_ok_;
    } else {
      ms_query = t_query.ElapsedMs();
    }

    SleepSec(api_gap_sec);
    Ticker t_cancel;
    if (CancelOrder(acc_id, market, order_id_)) {
      ms_cancel = t_cancel.ElapsedMs();
      ok_cancel = modify_ok_;
    } else {
      ms_cancel = t_cancel.ElapsedMs();
    }

    order_id = order_id_;
    return place_ok_ && ok_query && ok_cancel;
  }

  void OnReply_PlaceOrder(Moomoo::u32_t, const Trd_PlaceOrder::Response &stRsp) override {
    place_ok_ = (stRsp.rettype() == Common::RetType_Succeed);
    if (place_ok_) order_id_ = stRsp.s2c().orderid();
    else PrintUTF8(stRsp.retmsg());
    PostReply();
  }

  void OnReply_GetOrderList(Moomoo::u32_t, const Trd_GetOrderList::Response &stRsp) override {
    query_ok_ = (stRsp.rettype() == Common::RetType_Succeed);
    if (!query_ok_) PrintUTF8(stRsp.retmsg());
    PostReply();
  }

  void OnReply_ModifyOrder(Moomoo::u32_t, const Trd_ModifyOrder::Response &stRsp) override {
    modify_ok_ = (stRsp.rettype() == Common::RetType_Succeed);
    if (!modify_ok_) PrintUTF8(stRsp.retmsg());
    PostReply();
  }

 private:
  bool QueryOrder(uint64_t acc_id, MarketInfo market, const std::string &code, uint64_t order_id) {
    Trd_GetOrderList::Request list_req;
    Trd_GetOrderList::C2S *list_c2s = list_req.mutable_c2s();
    Trd_Common::TrdHeader *lhdr = list_c2s->mutable_header();
    lhdr->set_trdenv(Trd_Common::TrdEnv_Simulate);
    lhdr->set_accid(acc_id);
    lhdr->set_trdmarket(market.trd_market);
    list_c2s->mutable_filterconditions()->add_codelist(code);
    list_c2s->mutable_filterconditions()->add_idlist(order_id);
    list_c2s->set_refreshcache(true);
    query_ok_ = false;
    WaitReply(m_pTrdApi->GetOrderList(list_req));
    return query_ok_;
  }

  bool CancelOrder(uint64_t acc_id, MarketInfo market, uint64_t order_id) {
    Trd_ModifyOrder::Request mod_req;
    Trd_ModifyOrder::C2S *mod_c2s = mod_req.mutable_c2s();
    Trd_Common::TrdHeader *mhdr = mod_c2s->mutable_header();
    mhdr->set_trdenv(Trd_Common::TrdEnv_Simulate);
    mhdr->set_accid(acc_id);
    mhdr->set_trdmarket(market.trd_market);
    mod_c2s->set_orderid(order_id);
    mod_c2s->set_modifyorderop(Trd_Common::ModifyOrderOp_Cancel);
    mod_c2s->set_qty(0);
    mod_c2s->set_price(0);
    modify_ok_ = false;
    WaitReply(m_pTrdApi->ModifyOrder(mod_req));
    return modify_ok_;
  }

  uint64_t order_id_;
  bool place_ok_;
  bool query_ok_;
  bool modify_ok_;
};

int main(int argc, char **argv) {
  std::string host = "127.0.0.1";
  int port = 11121;
  std::string code = "US.AAPL";
  double qty = 1.0;
  double price = 0.01;
  int warm_iters = 10;
  double api_gap_sec = 0.5;
  double iter_gap_sec = 3.0;
  std::string out_path;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--host" && i + 1 < argc) host = argv[++i];
    else if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
    else if (a == "--code" && i + 1 < argc) code = argv[++i];
    else if (a == "--qty" && i + 1 < argc) qty = std::atof(argv[++i]);
    else if (a == "--price" && i + 1 < argc) price = std::atof(argv[++i]);
    else if (a == "--warm-iters" && i + 1 < argc) warm_iters = std::atoi(argv[++i]);
    else if (a == "--api-gap-sec" && i + 1 < argc) api_gap_sec = std::atof(argv[++i]);
    else if (a == "--iter-gap-sec" && i + 1 < argc) iter_gap_sec = std::atof(argv[++i]);
    else if (a == "--out" && i + 1 < argc) out_path = argv[++i];
  }
  if (out_path.empty()) {
    std::cerr << "usage: bench_query_cancel --out path.jsonl [--warm-iters N]" << std::endl;
    return 2;
  }

  MMAPI::Init();
  QueryCancelBench bench;
  Ticker t_conn;
  int rc = 0;
  int ok_count = 0;
  if (!bench.InitTrd(host.c_str(), static_cast<uint16_t>(port), "")) {
    std::cerr << "InitTrd failed" << std::endl;
    rc = 1;
  } else {
    double ms_connect = t_conn.ElapsedMs();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    for (int i = 1; i <= warm_iters; ++i) {
      double ms_place = 0, ms_query = 0, ms_cancel = 0;
      uint64_t oid = 0;
      bool ok_query = false, ok_cancel = false;
      Ticker t_all;
      bool ok = bench.RunOnce(code, qty, price, api_gap_sec, ms_place, ms_query, ms_cancel, oid, ok_query,
                              ok_cancel);
      ok_count += int(ok);
      double ms_total = t_all.ElapsedMs();
      std::ostringstream oss;
      oss << std::fixed << std::setprecision(3);
      oss << "{\"case\":\"query_cancel_warm\",\"lang\":\"cpp\",\"iter\":" << i << ",\"ok\":" << (ok ? "true" : "false")
          << ",\"ok_query\":" << (ok_query ? "true" : "false") << ",\"ok_cancel\":" << (ok_cancel ? "true" : "false")
          << ",\"order_id\":" << oid << ",\"ms_total\":" << ms_total
          << ",\"ms_connect\":" << (i == 1 ? ms_connect : 0.0) << ",\"ms_place\":" << ms_place
          << ",\"ms_query\":" << ms_query << ",\"ms_cancel\":" << ms_cancel << ",\"code\":\"" << JsonEscape(code)
          << "\",\"trd_env\":\"SIMULATE\"}";
      AppendJsonl(out_path, oss.str());
      std::cout << oss.str() << std::endl;
      if (!ok) rc = 1;
      if (i < warm_iters) SleepSec(iter_gap_sec);
    }
    bench.UnInitTrd();
  }

  std::cerr << "[cpp query_cancel] ok=" << ok_count << "/" << warm_iters << std::endl;
  protobuf::ShutdownProtobufLibrary();
  MMAPI::UnInit();
  return rc;
}
