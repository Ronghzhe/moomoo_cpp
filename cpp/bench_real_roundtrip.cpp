#include "FTAPIDemoBase.h"
#include "bench_jsonl_writer.hpp"

#include <algorithm>
#include <cctype>
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

bool IsFilled(int32_t status) {
  return status == Trd_Common::OrderStatus_Filled_All ||
         status == Trd_Common::OrderStatus_Filled_Part;
}

bool IsTerminalUnfilled(int32_t status) {
  return status == Trd_Common::OrderStatus_SubmitFailed ||
         status == Trd_Common::OrderStatus_Failed ||
         status == Trd_Common::OrderStatus_Disabled ||
         status == Trd_Common::OrderStatus_Deleted ||
         status == Trd_Common::OrderStatus_Cancelled_All ||
         status == Trd_Common::OrderStatus_Cancelled_Part;
}

std::string ToLowerCopy(const std::string &s) {
  std::string out = s;
  for (size_t i = 0; i < out.size(); ++i) {
    out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
  }
  return out;
}

bool IsRateLimitMsg(const std::string &msg) {
  std::string t = ToLowerCopy(msg);
  if (t.empty()) return false;
  return t.find("high frequency") != std::string::npos ||
         t.find("maximum 10 times per 30 seconds") != std::string::npos ||
         t.find("maximum 15 times per 30 seconds") != std::string::npos ||
         t.find("too many requests") != std::string::npos ||
         t.find("rate limit") != std::string::npos ||
         msg.find("频率") != std::string::npos ||
         msg.find("限频") != std::string::npos ||
         t.find("operating too fast") != std::string::npos ||
         msg.find("操作过快") != std::string::npos;
}

void SleepSec(double sec) {
  if (sec <= 0) return;
  std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(sec * 1000.0)));
}

}  // namespace

struct RateLimitConfig {
  double order_gap_sec;
  double query_gap_sec;
  double roundtrip_gap_sec;
  int retry_max;
  double retry_wait_sec;
};

class RealRoundtripBench : public FTAPIDemoBase {
 public:
  RealRoundtripBench()
      : order_id_(0),
        place_ok_(false),
        query_ok_(false),
        sub_acc_push_ok_(false),
        place_acc_id_(0),
        callback_hits_(0),
        target_order_id_(0),
        last_push_order_id_(0),
        last_push_ms_(0.0),
        first_order_push_ms_(0.0),
        t_place_start_(0.0),
        last_order_status_(0),
        last_retmsg_("") {}

  bool EnsureAccPush(uint64_t prefer_acc_id) {
    if (m_pTrdApi == nullptr) return false;
    m_pTrdApi->RegisterTrdSpi(this);
    m_pTrdApi->RegisterConnSpi(this);
    Trd_SubAccPush::Request req;
    Trd_SubAccPush::C2S *c2s = req.mutable_c2s();
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

  bool PlaceSideWithRetry(const std::string &code_full, Trd_Common::TrdSide side, double qty, double price,
                          const std::string &remark, uint64_t acc_id, MarketInfo market,
                          const RateLimitConfig &rl, double &ms_place, double &ms_order_push,
                          uint64_t &order_id, int &attempts) {
    attempts = 0;
    for (int i = 1; i <= rl.retry_max + 1; ++i) {
      attempts = i;
      SleepSec(rl.order_gap_sec);
      if (PlaceSide(code_full, side, qty, price, remark, acc_id, market, ms_place, ms_order_push, order_id)) {
        return true;
      }
      if (!IsRateLimitMsg(last_retmsg_) || i > rl.retry_max) {
        std::cerr << "place failed side=" << side << " msg=" << last_retmsg_ << std::endl;
        return false;
      }
      double wait = rl.retry_wait_sec * i;
      std::cerr << "[rate_limit] place retry attempt=" << i << "/" << rl.retry_max << " wait=" << wait << "s"
                << std::endl;
      SleepSec(wait);
    }
    return false;
  }

  bool PlaceSide(const std::string &code_full, Trd_Common::TrdSide side, double qty, double price,
                 const std::string &remark, uint64_t acc_id, MarketInfo market, double &ms_place,
                 double &ms_order_push, uint64_t &order_id) {
    ResetPushState();
    order_id_ = 0;
    place_ok_ = false;
    target_order_id_ = 0;
    std::string code = ParseCode(code_full);

    Trd_PlaceOrder::Request place_req;
    Trd_PlaceOrder::C2S *place_c2s = place_req.mutable_c2s();
    Trd_Common::TrdHeader *hdr = place_c2s->mutable_header();
    hdr->set_trdenv(Trd_Common::TrdEnv_Real);
    hdr->set_accid(acc_id);
    hdr->set_trdmarket(market.trd_market);
    place_c2s->set_trdside(side);
    place_c2s->set_ordertype(Trd_Common::OrderType_Normal);
    place_c2s->set_code(code);
    place_c2s->set_qty(qty);
    place_c2s->set_price(price);
    place_c2s->set_secmarket(market.sec_market);
    place_c2s->set_remark(remark);

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
    }
    order_id = order_id_;
    return true;
  }

  bool QueryOrderStatusWithRetry(uint64_t acc_id, MarketInfo market, const std::string &code,
                                 uint64_t order_id, const RateLimitConfig &rl, int32_t &status_out) {
    for (int i = 1; i <= rl.retry_max + 1; ++i) {
      if (QueryOrderStatus(acc_id, market, code, order_id, status_out) && query_ok_) {
        return true;
      }
      if (!IsRateLimitMsg(last_retmsg_) || i > rl.retry_max) {
        return query_ok_;
      }
      double wait = rl.retry_wait_sec * i;
      std::cerr << "[rate_limit] query retry attempt=" << i << "/" << rl.retry_max << " wait=" << wait << "s"
                << std::endl;
      SleepSec(wait);
    }
    return query_ok_;
  }

  bool WaitFill(uint64_t acc_id, MarketInfo market, const std::string &code_full, uint64_t order_id,
                double timeout_sec, const RateLimitConfig &rl, double &ms_wait, int32_t &final_status) {
    Ticker t_wait;
    std::string code = ParseCode(code_full);
    final_status = 0;
    while (t_wait.ElapsedMs() < timeout_sec * 1000.0) {
      SleepSec(rl.query_gap_sec);
      if (!QueryOrderStatusWithRetry(acc_id, market, code, order_id, rl, final_status)) {
        continue;
      }
      if (IsFilled(final_status)) {
        ms_wait = t_wait.ElapsedMs();
        return true;
      }
      if (IsTerminalUnfilled(final_status)) {
        ms_wait = t_wait.ElapsedMs();
        return false;
      }
    }
    QueryOrderStatusWithRetry(acc_id, market, code, order_id, rl, final_status);
    ms_wait = t_wait.ElapsedMs();
    return IsFilled(final_status);
  }

  bool QueryOrderStatus(uint64_t acc_id, MarketInfo market, const std::string &code, uint64_t order_id,
                        int32_t &status_out) {
    Trd_GetOrderList::Request list_req;
    Trd_GetOrderList::C2S *list_c2s = list_req.mutable_c2s();
    Trd_Common::TrdHeader *lhdr = list_c2s->mutable_header();
    lhdr->set_trdenv(Trd_Common::TrdEnv_Real);
    lhdr->set_accid(acc_id);
    lhdr->set_trdmarket(market.trd_market);
    list_c2s->mutable_filterconditions()->add_codelist(code);
    list_c2s->mutable_filterconditions()->add_idlist(order_id);
    list_c2s->set_refreshcache(true);
    query_ok_ = false;
    last_order_status_ = 0;
    WaitReply(m_pTrdApi->GetOrderList(list_req));
    if (!query_ok_) return false;
    status_out = last_order_status_;
    return true;
  }

  bool CancelOrder(uint64_t acc_id, MarketInfo market, uint64_t order_id) {
    Trd_ModifyOrder::Request mod_req;
    Trd_ModifyOrder::C2S *mod_c2s = mod_req.mutable_c2s();
    Trd_Common::TrdHeader *mhdr = mod_c2s->mutable_header();
    mhdr->set_trdenv(Trd_Common::TrdEnv_Real);
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

  bool RunRoundtrip(const std::string &code_full, double qty, double buy_price, double sell_price,
                    double fill_timeout_sec, const RateLimitConfig &rl, double &ms_buy_place,
                    double &ms_buy_push, double &ms_buy_fill_wait, double &ms_sell_place, double &ms_sell_push,
                    double &ms_sell_fill_wait, uint64_t &buy_oid, uint64_t &sell_oid, int32_t &buy_status,
                    int32_t &sell_status, int &buy_attempts, int &sell_attempts) {
    MarketInfo market = ParseMarket(code_full);
    uint64_t acc_id = GetFirstAccID(market.trd_market, Trd_Common::TrdEnv_Real);
    place_acc_id_ = acc_id;
    if (acc_id == 0) {
      std::cerr << "no " << market.name << " REAL account" << std::endl;
      return false;
    }
    EnsureAccPush(acc_id);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    if (!PlaceSideWithRetry(code_full, Trd_Common::TrdSide_Buy, qty, buy_price, "bench_real_buy_cpp", acc_id, market,
                            rl, ms_buy_place, ms_buy_push, buy_oid, buy_attempts)) {
      return false;
    }
    if (!WaitFill(acc_id, market, code_full, buy_oid, fill_timeout_sec, rl, ms_buy_fill_wait, buy_status)) {
      CancelOrder(acc_id, market, buy_oid);
      return false;
    }

    SleepSec(rl.roundtrip_gap_sec);

    if (!PlaceSideWithRetry(code_full, Trd_Common::TrdSide_Sell, qty, sell_price, "bench_real_sell_cpp", acc_id,
                            market, rl, ms_sell_place, ms_sell_push, sell_oid, sell_attempts)) {
      return false;
    }
    if (!WaitFill(acc_id, market, code_full, sell_oid, fill_timeout_sec, rl, ms_sell_fill_wait, sell_status)) {
      CancelOrder(acc_id, market, sell_oid);
      return false;
    }
    return true;
  }

  void OnReply_PlaceOrder(Moomoo::u32_t, const Trd_PlaceOrder::Response &stRsp) override {
    place_ok_ = (stRsp.rettype() == Common::RetType_Succeed);
    last_retmsg_ = stRsp.retmsg();
    if (place_ok_) order_id_ = stRsp.s2c().orderid();
    else PrintUTF8(stRsp.retmsg());
    PostReply();
  }

  void OnReply_GetOrderList(Moomoo::u32_t, const Trd_GetOrderList::Response &stRsp) override {
    query_ok_ = (stRsp.rettype() == Common::RetType_Succeed);
    last_retmsg_ = stRsp.retmsg();
    if (query_ok_ && stRsp.s2c().orderlist_size() > 0) {
      last_order_status_ = stRsp.s2c().orderlist(0).orderstatus();
    } else if (!query_ok_) {
      PrintUTF8(stRsp.retmsg());
    }
    PostReply();
  }

  void OnReply_ModifyOrder(Moomoo::u32_t, const Trd_ModifyOrder::Response &stRsp) override {
    modify_ok_ = (stRsp.rettype() == Common::RetType_Succeed);
    if (!modify_ok_) PrintUTF8(stRsp.retmsg());
    PostReply();
  }

  void OnReply_SubAccPush(Moomoo::u32_t, const Trd_SubAccPush::Response &stRsp) override {
    sub_acc_push_ok_ = (stRsp.rettype() == Common::RetType_Succeed);
    if (!sub_acc_push_ok_) PrintUTF8(stRsp.retmsg());
    PostReply();
  }

  void OnPush_UpdateOrder(const Trd_UpdateOrder::Response &stRsp) override {
    std::lock_guard<std::mutex> lk(push_mu_);
    callback_hits_ += 1;
    uint64_t oid = stRsp.s2c().order().orderid();
    last_push_order_id_ = oid;
    last_push_ms_ = (t_place_start_ > 0.0) ? (NowMs() - t_place_start_) : 0.001;
    if (target_order_id_ != 0 && oid == target_order_id_ && first_order_push_ms_ <= 0.0) {
      first_order_push_ms_ = last_push_ms_;
    }
    push_cv_.notify_all();
  }

 private:
  void ResetPushState() {
    target_order_id_ = 0;
    last_push_order_id_ = 0;
    last_push_ms_ = 0.0;
    first_order_push_ms_ = 0.0;
    t_place_start_ = 0.0;
    last_retmsg_.clear();
  }

  uint64_t order_id_;
  bool place_ok_;
  bool query_ok_;
  bool modify_ok_;
  bool sub_acc_push_ok_;
  uint64_t place_acc_id_;
  int callback_hits_;
  uint64_t target_order_id_;
  uint64_t last_push_order_id_;
  double last_push_ms_;
  double first_order_push_ms_;
  double t_place_start_;
  int32_t last_order_status_;
  std::string last_retmsg_;
  std::mutex push_mu_;
  std::condition_variable push_cv_;
};

int main(int argc, char **argv) {
  std::string host = "127.0.0.1";
  int port = 11121;
  std::string code = "HK.02208";
  double qty = 200.0;
  double buy_price = 0.0;
  double sell_price = 0.0;
  double fill_timeout_sec = 60.0;
  double order_gap_sec = 2.5;
  double query_gap_sec = 1.0;
  double roundtrip_gap_sec = 3.0;
  int rate_limit_retry_max = 5;
  double rate_limit_retry_wait_sec = 2.5;
  std::string pwd_md5;
  std::string out_path;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--host" && i + 1 < argc) host = argv[++i];
    else if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
    else if (a == "--code" && i + 1 < argc) code = argv[++i];
    else if (a == "--qty" && i + 1 < argc) qty = std::atof(argv[++i]);
    else if (a == "--buy-price" && i + 1 < argc) buy_price = std::atof(argv[++i]);
    else if (a == "--sell-price" && i + 1 < argc) sell_price = std::atof(argv[++i]);
    else if (a == "--fill-timeout-sec" && i + 1 < argc) fill_timeout_sec = std::atof(argv[++i]);
    else if (a == "--order-gap-sec" && i + 1 < argc) order_gap_sec = std::atof(argv[++i]);
    else if (a == "--query-gap-sec" && i + 1 < argc) query_gap_sec = std::atof(argv[++i]);
    else if (a == "--roundtrip-gap-sec" && i + 1 < argc) roundtrip_gap_sec = std::atof(argv[++i]);
    else if (a == "--rate-limit-retry-max" && i + 1 < argc) rate_limit_retry_max = std::atoi(argv[++i]);
    else if (a == "--rate-limit-retry-wait-sec" && i + 1 < argc) rate_limit_retry_wait_sec = std::atof(argv[++i]);
    else if (a == "--pwd-md5" && i + 1 < argc) pwd_md5 = argv[++i];
    else if (a == "--out" && i + 1 < argc) out_path = argv[++i];
  }

  if (out_path.empty() || buy_price <= 0 || sell_price <= 0 || pwd_md5.empty()) {
    std::cerr << "usage: bench_real_roundtrip --out path.jsonl --buy-price X --sell-price Y --pwd-md5 MD5"
              << std::endl;
    return 2;
  }

  BenchJsonlWriter jsonl_out(out_path);

  MMAPI::Init();
  RealRoundtripBench bench;
  Ticker t_conn;
  int rc = 0;
  if (!bench.InitTrd(host.c_str(), static_cast<uint16_t>(port), pwd_md5.c_str())) {
    std::cerr << "InitTrd REAL failed" << std::endl;
    rc = 1;
  } else {
    double ms_buy_place = 0, ms_buy_push = 0, ms_buy_fill = 0;
    double ms_sell_place = 0, ms_sell_push = 0, ms_sell_fill = 0;
    uint64_t buy_oid = 0, sell_oid = 0;
    int32_t buy_status = 0, sell_status = 0;
    int buy_attempts = 0, sell_attempts = 0;
    RateLimitConfig rl = {order_gap_sec, query_gap_sec, roundtrip_gap_sec, rate_limit_retry_max,
                          rate_limit_retry_wait_sec};
    bool ok = bench.RunRoundtrip(code, qty, buy_price, sell_price, fill_timeout_sec, rl, ms_buy_place, ms_buy_push,
                                 ms_buy_fill, ms_sell_place, ms_sell_push, ms_sell_fill, buy_oid, sell_oid,
                                 buy_status, sell_status, buy_attempts, sell_attempts);
    double ms_connect = t_conn.ElapsedMs();
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);
    oss << "{\"case\":\"real_roundtrip\",\"lang\":\"cpp\",\"ok\":" << (ok ? "true" : "false")
        << ",\"code\":\"" << JsonEscape(code) << "\",\"qty\":" << qty << ",\"trd_env\":\"REAL\""
        << ",\"ms_connect\":" << ms_connect << ",\"buy_price\":" << buy_price << ",\"sell_price\":" << sell_price
        << ",\"order_gap_sec\":" << order_gap_sec << ",\"query_gap_sec\":" << query_gap_sec
        << ",\"rate_limit_retry_max\":" << rate_limit_retry_max
        << ",\"buy_ms_place\":" << ms_buy_place << ",\"buy_ms_order_push\":" << ms_buy_push
        << ",\"buy_place_attempts\":" << buy_attempts << ",\"ms_buy_fill_wait\":" << ms_buy_fill
        << ",\"buy_order_id\":" << buy_oid << ",\"buy_order_status\":" << buy_status
        << ",\"sell_ms_place\":" << ms_sell_place << ",\"sell_ms_order_push\":" << ms_sell_push
        << ",\"sell_place_attempts\":" << sell_attempts << ",\"ms_sell_fill_wait\":" << ms_sell_fill
        << ",\"sell_order_id\":" << sell_oid << ",\"sell_order_status\":" << sell_status << "}";
    jsonl_out.AppendLine(oss.str());
    std::cout << oss.str() << std::endl;
    if (!ok) rc = 1;
    bench.UnInitTrd();
  }

  protobuf::ShutdownProtobufLibrary();
  MMAPI::UnInit();
  return rc;
}
