#include "FTAPIDemoBase.h"
#include "bench_jsonl_writer.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <mutex>
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

}  // namespace

class OrderBench : public FTAPIDemoBase {
 public:
  OrderBench() : order_id_(0), place_ok_(false), modify_ok_(false), query_ok_(false), sub_acc_push_ok_(false),
                 place_acc_id_(0), callback_hits_(0),
                 target_order_id_(0), last_push_order_id_(0), last_push_ms_(0.0),
                 first_order_push_ms_(0.0), first_any_push_ms_(0.0), first_any_push_order_id_(0),
                 any_push_count_(0), t_place_start_(0.0) {}

  // Docs: RegisterTrdSpi + InitConnect + SubAccPush(accIDList). Do NOT Unregister after subscribe.
  bool EnsureAccPush(uint64_t prefer_acc_id) {
    if (m_pTrdApi == nullptr) return false;
    m_pTrdApi->RegisterTrdSpi(this);
    m_pTrdApi->RegisterConnSpi(this);

    Trd_SubAccPush::Request req;
    Trd_SubAccPush::C2S *c2s = req.mutable_c2s();
    // Protocol is full-replace: pass every account that should receive push.
    if (!m_setAllAccID.empty()) {
      for (auto it = m_setAllAccID.begin(); it != m_setAllAccID.end(); ++it) {
        c2s->add_accidlist(*it);
      }
    } else if (prefer_acc_id != 0) {
      c2s->add_accidlist(prefer_acc_id);
    } else {
      return false;
    }
    sub_acc_push_ok_ = false;
    WaitReply(m_pTrdApi->SubAccPush(req));
    return sub_acc_push_ok_;
  }

  bool RunOnce(const std::string &code_full, double qty, double price, double &ms_place,
               double &ms_query, double &ms_cancel, double &ms_order_push, uint64_t &order_id,
               int &push_any_count, uint64_t &push_any_order_id, double &ms_push_any,
               uint64_t &place_acc_id, int &callback_hits) {
    order_id_ = 0;
    place_ok_ = false;
    modify_ok_ = false;
    query_ok_ = false;
    target_order_id_ = 0;
    last_push_order_id_ = 0;
    last_push_ms_ = 0.0;
    first_order_push_ms_ = 0.0;
    first_any_push_ms_ = 0.0;
    first_any_push_order_id_ = 0;
    any_push_count_ = 0;
    callback_hits_ = 0;
    std::string code = ParseCode(code_full);
    MarketInfo market = ParseMarket(code_full);
    uint64_t acc_id = GetFirstAccID(market.trd_market, Trd_Common::TrdEnv_Simulate);
    place_acc_id_ = acc_id;
    place_acc_id = acc_id;
    if (acc_id == 0) {
      std::cerr << "no " << market.name << " simulate account" << std::endl;
      return false;
    }
    // Re-subscribe on this connection right before placing, matching official C++ example.
    EnsureAccPush(acc_id);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

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
    place_c2s->set_remark("bench_cpp");

    Ticker t_place;
    t_place_start_ = NowMs();
    WaitReply(m_pTrdApi->PlaceOrder(place_req));
    ms_place = t_place.ElapsedMs();
    if (!place_ok_ || order_id_ == 0) return false;
    target_order_id_ = order_id_;
    {
      std::unique_lock<std::mutex> lk(push_mu_);
      if (last_push_order_id_ == target_order_id_ && first_order_push_ms_ <= 0.0) {
        first_order_push_ms_ = last_push_ms_;
      }
      push_cv_.wait_for(lk, std::chrono::milliseconds(5000), [&] {
        return first_order_push_ms_ > 0.0;
      });
      ms_order_push = first_order_push_ms_;
      push_any_count = any_push_count_;
      push_any_order_id = first_any_push_order_id_;
      ms_push_any = first_any_push_ms_;
      callback_hits = callback_hits_;
    }

    Trd_GetOrderList::Request list_req;
    Trd_GetOrderList::C2S *list_c2s = list_req.mutable_c2s();
    Trd_Common::TrdHeader *lhdr = list_c2s->mutable_header();
    lhdr->set_trdenv(Trd_Common::TrdEnv_Simulate);
    lhdr->set_accid(acc_id);
    lhdr->set_trdmarket(market.trd_market);
    list_c2s->mutable_filterconditions()->add_codelist(code);
    list_c2s->mutable_filterconditions()->add_idlist(order_id_);
    list_c2s->set_refreshcache(true);
    Ticker t_query;
    WaitReply(m_pTrdApi->GetOrderList(list_req));
    ms_query = t_query.ElapsedMs();

    Trd_ModifyOrder::Request mod_req;
    Trd_ModifyOrder::C2S *mod_c2s = mod_req.mutable_c2s();
    Trd_Common::TrdHeader *mhdr = mod_c2s->mutable_header();
    mhdr->set_trdenv(Trd_Common::TrdEnv_Simulate);
    mhdr->set_accid(acc_id);
    mhdr->set_trdmarket(market.trd_market);
    mod_c2s->set_orderid(order_id_);
    mod_c2s->set_modifyorderop(Trd_Common::ModifyOrderOp_Cancel);
    mod_c2s->set_qty(0);
    mod_c2s->set_price(0);
    Ticker t_cancel;
    WaitReply(m_pTrdApi->ModifyOrder(mod_req));
    ms_cancel = t_cancel.ElapsedMs();

    order_id = order_id_;
    return place_ok_ && modify_ok_;
  }

  void OnReply_PlaceOrder(Moomoo::u32_t nSerialNo, const Trd_PlaceOrder::Response &stRsp) override {
    place_ok_ = (stRsp.rettype() == Common::RetType_Succeed);
    if (place_ok_) order_id_ = stRsp.s2c().orderid();
    else PrintUTF8(stRsp.retmsg());
    PostReply();
  }

  void OnReply_GetOrderList(Moomoo::u32_t nSerialNo, const Trd_GetOrderList::Response &stRsp) override {
    query_ok_ = (stRsp.rettype() == Common::RetType_Succeed);
    if (!query_ok_) PrintUTF8(stRsp.retmsg());
    PostReply();
  }

  void OnReply_ModifyOrder(Moomoo::u32_t nSerialNo, const Trd_ModifyOrder::Response &stRsp) override {
    modify_ok_ = (stRsp.rettype() == Common::RetType_Succeed);
    if (!modify_ok_) PrintUTF8(stRsp.retmsg());
    PostReply();
  }

  void OnReply_SubAccPush(Moomoo::u32_t, const Trd_SubAccPush::Response &stRsp) override {
    sub_acc_push_ok_ = (stRsp.rettype() == Common::RetType_Succeed);
    if (!sub_acc_push_ok_) PrintUTF8(stRsp.retmsg());
    PostReply();
  }

  bool sub_acc_push_ok() const { return sub_acc_push_ok_; }
  uint64_t place_acc_id() const { return place_acc_id_; }

  void OnPush_UpdateOrder(const Trd_UpdateOrder::Response &stRsp) override {
    // Always count callback entry first. Official sample does not gate on has_s2c().
    std::lock_guard<std::mutex> lk(push_mu_);
    callback_hits_ += 1;
    // Match official PlaceOrderDemo: read s2c/order directly (required fields).
    uint64_t oid = stRsp.s2c().order().orderid();
    any_push_count_ += 1;
    if (first_any_push_ms_ <= 0.0) {
      first_any_push_order_id_ = oid;
      first_any_push_ms_ = (t_place_start_ > 0.0) ? (NowMs() - t_place_start_) : 0.001;
    }
    last_push_order_id_ = oid;
    last_push_ms_ = (t_place_start_ > 0.0) ? (NowMs() - t_place_start_) : 0.001;
    if (target_order_id_ != 0 && oid == target_order_id_ && first_order_push_ms_ <= 0.0) {
      first_order_push_ms_ = last_push_ms_;
    }
    // Wake waiter for any push; matching happens when reading results.
    if (first_order_push_ms_ > 0.0 || any_push_count_ > 0) {
      push_cv_.notify_all();
    }
  }

  void OnPush_UpdateOrderFill(const Trd_UpdateOrderFill::Response &stRsp) override {
    std::lock_guard<std::mutex> lk(push_mu_);
    callback_hits_ += 1;
    any_push_count_ += 1;
    if (first_any_push_ms_ <= 0.0) {
      first_any_push_ms_ = (t_place_start_ > 0.0) ? (NowMs() - t_place_start_) : 0.001;
      if (stRsp.has_s2c() && stRsp.s2c().has_orderfill()) {
        first_any_push_order_id_ = stRsp.s2c().orderfill().orderid();
      }
    }
    push_cv_.notify_all();
  }

 private:
  uint64_t order_id_;
  bool place_ok_;
  bool modify_ok_;
  bool query_ok_;
  bool sub_acc_push_ok_;
  uint64_t place_acc_id_;
  int callback_hits_;
  uint64_t target_order_id_;
  uint64_t last_push_order_id_;
  double last_push_ms_;
  double first_order_push_ms_;
  double first_any_push_ms_;
  uint64_t first_any_push_order_id_;
  int any_push_count_;
  double t_place_start_;
  std::mutex push_mu_;
  std::condition_variable push_cv_;
};

int main(int argc, char **argv) {
  std::string host = "127.0.0.1";
  int port = 11121;
  std::string code = "US.AAPL";
  double qty = 1.0;
  double price = 0.01;
  int warm_iters = 30;
  int cold_iters = 5;
  std::string mode = "both";
  std::string out_path;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--host" && i + 1 < argc) host = argv[++i];
    else if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
    else if (a == "--code" && i + 1 < argc) code = argv[++i];
    else if (a == "--qty" && i + 1 < argc) qty = std::atof(argv[++i]);
    else if (a == "--price" && i + 1 < argc) price = std::atof(argv[++i]);
    else if (a == "--warm-iters" && i + 1 < argc) warm_iters = std::atoi(argv[++i]);
    else if (a == "--cold-iters" && i + 1 < argc) cold_iters = std::atoi(argv[++i]);
    else if (a == "--mode" && i + 1 < argc) mode = argv[++i];
    else if (a == "--out" && i + 1 < argc) out_path = argv[++i];
  }
  if (out_path.empty()) {
    std::cerr << "usage: bench_order --out path.jsonl [--mode warm|cold|both]" << std::endl;
    return 2;
  }

  BenchJsonlWriter jsonl_out(out_path);

  MMAPI::Init();
  int rc = 0;

  auto run_iter = [&](OrderBench &bench, int iter, bool cold, double ms_connect) {
    double ms_place = 0, ms_query = 0, ms_cancel = 0, ms_order_push = 0, ms_push_any = 0;
    uint64_t oid = 0;
    uint64_t push_any_oid = 0;
    uint64_t place_acc_id = 0;
    int push_any_count = 0;
    int callback_hits = 0;
    Ticker t_all;
    bool ok = bench.RunOnce(code, qty, price, ms_place, ms_query, ms_cancel, ms_order_push, oid,
                            push_any_count, push_any_oid, ms_push_any, place_acc_id, callback_hits);
    double ms_total = t_all.ElapsedMs();
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);
    oss << "{\"case\":\"" << (cold ? "order_cold" : "order_warm")
        << "\",\"lang\":\"cpp\",\"iter\":" << iter << ",\"ok\":" << (ok ? "true" : "false")
        << ",\"order_id\":" << oid << ",\"ms_total\":" << ms_total
        << ",\"ms_connect\":" << ms_connect << ",\"ms_place\":" << ms_place
        << ",\"ms_query\":" << ms_query << ",\"ms_cancel\":" << ms_cancel
        << ",\"ms_order_push\":" << ms_order_push
        << ",\"sub_acc_push_ok\":" << (bench.sub_acc_push_ok() ? "true" : "false")
        << ",\"place_acc_id\":" << place_acc_id
        << ",\"callback_hits\":" << callback_hits
        << ",\"push_any_count\":" << push_any_count
        << ",\"push_any_order_id\":" << push_any_oid
        << ",\"ms_push_any\":" << ms_push_any
        << ",\"code\":\"" << JsonEscape(code) << "\",\"trd_env\":\"SIMULATE\"}";
    jsonl_out.AppendLine(oss.str());
    std::cout << oss.str() << std::endl;
    if (!ok) rc = 1;
  };

  if (mode == "cold" || mode == "both") {
    for (int i = 1; i <= cold_iters; ++i) {
      OrderBench bench;
      Ticker t_conn;
      if (!bench.InitTrd(host.c_str(), static_cast<uint16_t>(port), "")) {
        std::cerr << "cold InitTrd failed iter=" << i << std::endl;
        rc = 1;
        continue;
      }
      run_iter(bench, i, true, t_conn.ElapsedMs());
      bench.UnInitTrd();
    }
  }

  if (mode == "warm" || mode == "both") {
    OrderBench bench;
    Ticker t_conn;
    if (!bench.InitTrd(host.c_str(), static_cast<uint16_t>(port), "")) {
      std::cerr << "InitTrd failed" << std::endl;
      rc = 1;
    } else {
      double ms_connect = t_conn.ElapsedMs();
      // Keep RegisterTrdSpi from constructor/Init; do not Unregister mid-session.
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      for (int i = 1; i <= warm_iters; ++i) {
        run_iter(bench, i, false, i == 1 ? ms_connect : 0.0);
      }
      bench.UnInitTrd();
    }
  }

  protobuf::ShutdownProtobufLibrary();
  MMAPI::UnInit();
  return rc;
}
