#include "opend/quote_connection.hpp"

#include "opend/connect_guard.hpp"
#include "quote/kline_parse.hpp"

#include "Proto/Qot_Common.pb.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace mmtrade {
namespace opend {

namespace {

std::string SecurityToFullCode(const Qot_Common::Security &sec) {
  std::string prefix = "US";
  if (sec.market() == Qot_Common::QotMarket_HK_Security) {
    prefix = "HK";
  }
  return prefix + "." + sec.code();
}

double FirstBookPrice(
    const google::protobuf::RepeatedPtrField<Qot_Common::OrderBook> &books) {
  if (books.size() <= 0) {
    return 0.0;
  }
  return books.Get(0).price();
}

double BookVolume(const Qot_Common::OrderBook &book) {
  return book.has_hpvolume() ? book.hpvolume()
                             : static_cast<double>(book.volume());
}

void ApplyBookToCache(const Qot_Common::Security &sec,
                      const google::protobuf::RepeatedPtrField<Qot_Common::OrderBook> &bids,
                      const google::protobuf::RepeatedPtrField<Qot_Common::OrderBook> &asks,
                      std::int64_t exchange_ts_ms,
                      std::map<std::string, QuoteSnapshot> *cache) {
  if (cache == NULL) {
    return;
  }
  const double bid = FirstBookPrice(bids);
  const double ask = FirstBookPrice(asks);
  if (bid <= 0.0 || ask <= 0.0) {
    return;
  }
  const std::string key = SecurityToFullCode(sec);
  QuoteSnapshot snap = (*cache)[key];
  snap.bid_1 = bid;
  snap.ask_1 = ask;
  snap.bid_count =
      std::min(bids.size(), QuoteSnapshot::kMaxDepthLevels);
  snap.ask_count =
      std::min(asks.size(), QuoteSnapshot::kMaxDepthLevels);
  for (int i = 0; i < snap.bid_count; ++i) {
    snap.bid_prices[i] = bids.Get(i).price();
    snap.bid_volumes[i] = BookVolume(bids.Get(i));
  }
  for (int i = 0; i < snap.ask_count; ++i) {
    snap.ask_prices[i] = asks.Get(i).price();
    snap.ask_volumes[i] = BookVolume(asks.Get(i));
  }
  snap.bid_1_volume = snap.bid_volumes[0];
  snap.ask_1_volume = snap.ask_volumes[0];
  snap.valid = true;
  using namespace std::chrono;
  snap.local_ts_ms = duration_cast<milliseconds>(
                          system_clock::now().time_since_epoch())
                          .count();
  snap.exchange_ts_ms = exchange_ts_ms;
  (*cache)[key] = snap;
}

std::int64_t OpenDServerTimestampMs(double timestamp) {
  if (timestamp <= 0.0) {
    return 0;
  }
  // OpenD 可能返回秒或毫秒时间戳；美股盘口该字段可能缺失或仅在档位变化时更新。
  return static_cast<std::int64_t>(timestamp < 100000000000.0
                                       ? timestamp * 1000.0
                                       : timestamp);
}

}  // namespace

QuoteConnection::QuoteConnection()
    : connected_(false),
      sub_ok_(false),
      get_ok_(false),
      snap_ok_(false),
      snap_open_(0.0),
      snap_lot_size_(0),
      snap_last_price_(0.0),
      snap_sec_status_(0),
      snap_suspended_(false),
      book_push_seq_(0),
      kline_get_ok_(false) {}

bool QuoteConnection::Connect(const std::string &host, int port) {
  Disconnect();
  {
    OpenDConnectGuard guard;
    if (!InitQot(host.c_str(), static_cast<uint16_t>(port))) {
      return false;
    }
  }
  connected_ = true;
  MaybeSleepAfterOpenDConnect();
  return true;
}

void QuoteConnection::Disconnect() {
  if (connected_) {
    UnInitQot();
    connected_ = false;
  }
}

void QuoteConnection::DisconnectTrade() { TradeConnection::Disconnect(); }

Qot_Common::QotMarket QuoteConnection::ToSdkMarket(const std::string &code_full) const {
  const quote::QuoteSymbol sym = quote::ParseQuoteSymbol(code_full);
  if (sym.prefix == "HK") {
    return Qot_Common::QotMarket_HK_Security;
  }
  return Qot_Common::QotMarket_US_Security;
}

std::string QuoteConnection::SdkCode(const std::string &code_full) const {
  return quote::ParseQuoteSymbol(code_full).code;
}

bool QuoteConnection::SubscribeOrderBook(const std::vector<std::string> &codes_full) {
  if (!connected_ || codes_full.empty()) {
    return false;
  }
  sub_ok_ = false;
  Qot_Sub::Request req;
  Qot_Sub::C2S *c2s = req.mutable_c2s();
  for (size_t i = 0; i < codes_full.size(); ++i) {
    Qot_Common::Security *sec = c2s->add_securitylist();
    sec->set_market(ToSdkMarket(codes_full[i]));
    sec->set_code(SdkCode(codes_full[i]));
  }
  c2s->add_subtypelist(Qot_Common::SubType_OrderBook);
  c2s->set_issuborunsub(true);
  c2s->set_isregorunregpush(true);
  c2s->set_extendedtime(true);
  WaitReply(m_pQotApi->Sub(req));
  return sub_ok_;
}

bool QuoteConnection::UnsubscribeOrderBook(const std::vector<std::string> &codes_full) {
  if (!connected_ || codes_full.empty()) {
    return false;
  }
  sub_ok_ = false;
  Qot_Sub::Request req;
  Qot_Sub::C2S *c2s = req.mutable_c2s();
  for (size_t i = 0; i < codes_full.size(); ++i) {
    Qot_Common::Security *sec = c2s->add_securitylist();
    sec->set_market(ToSdkMarket(codes_full[i]));
    sec->set_code(SdkCode(codes_full[i]));
  }
  c2s->add_subtypelist(Qot_Common::SubType_OrderBook);
  c2s->set_issuborunsub(false);
  c2s->set_isregorunregpush(false);
  c2s->set_extendedtime(true);
  WaitReply(m_pQotApi->Sub(req));
  for (size_t i = 0; i < codes_full.size(); ++i) {
    std::lock_guard<std::mutex> lk(mu_);
    cache_.erase(codes_full[i]);
  }
  return sub_ok_;
}

void QuoteConnection::ClearQuoteCache() {
  std::lock_guard<std::mutex> lk(mu_);
  cache_.clear();
}

bool QuoteConnection::RefreshOrderBook(const std::string &code_full) {
  if (!connected_) {
    return false;
  }
  get_ok_ = false;
  Qot_GetOrderBook::Request req;
  Qot_GetOrderBook::C2S *c2s = req.mutable_c2s();
  Qot_Common::Security *sec = c2s->mutable_security();
  sec->set_market(ToSdkMarket(code_full));
  sec->set_code(SdkCode(code_full));
  c2s->set_num(QuoteSnapshot::kMaxDepthLevels);
  WaitReply(m_pQotApi->GetOrderBook(req));
  return get_ok_;
}

bool QuoteConnection::RefreshMarketSnapshot(const std::string &code_full) {
  if (!connected_) {
    return false;
  }
  snap_ok_ = false;
  snap_open_ = 0.0;
  snap_lot_size_ = 0;
  snap_last_price_ = 0.0;
  Qot_GetSecuritySnapshot::Request req;
  Qot_GetSecuritySnapshot::C2S *c2s = req.mutable_c2s();
  Qot_Common::Security *sec = c2s->add_securitylist();
  sec->set_market(ToSdkMarket(code_full));
  sec->set_code(SdkCode(code_full));
  WaitReply(m_pQotApi->GetSecuritySnapshot(req));
  if (!snap_ok_) {
    return false;
  }
  std::lock_guard<std::mutex> lk(mu_);
  QuoteSnapshot snap = cache_[code_full];
  if (snap_open_ > 0.0) {
    snap.day_open = snap_open_;
    snap.has_day_open = true;
  }
  if (snap_lot_size_ > 0) {
    snap.lot_size = snap_lot_size_;
  }
  if (snap_last_price_ > 0.0) {
    snap.last_price = snap_last_price_;
  }
  snap.sec_status = snap_sec_status_;
  snap.suspended = snap_suspended_;
  snap.has_tradability = true;
  cache_[code_full] = snap;
  return true;
}

double QuoteConnection::FetchDayOpenPrice(const std::string &code_full) {
  if (!connected_) {
    return 0.0;
  }
  snap_ok_ = false;
  snap_open_ = 0.0;
  snap_lot_size_ = 0;
  snap_last_price_ = 0.0;
  Qot_GetSecuritySnapshot::Request req;
  Qot_GetSecuritySnapshot::C2S *c2s = req.mutable_c2s();
  Qot_Common::Security *sec = c2s->add_securitylist();
  sec->set_market(ToSdkMarket(code_full));
  sec->set_code(SdkCode(code_full));
  WaitReply(m_pQotApi->GetSecuritySnapshot(req));
  if (!snap_ok_ || snap_open_ <= 0.0) {
    return 0.0;
  }
  std::lock_guard<std::mutex> lk(mu_);
  QuoteSnapshot snap = cache_[code_full];
  snap.day_open = snap_open_;
  snap.has_day_open = true;
  if (snap_lot_size_ > 0) {
    snap.lot_size = snap_lot_size_;
  }
  if (snap_last_price_ > 0.0) {
    snap.last_price = snap_last_price_;
  }
  snap.sec_status = snap_sec_status_;
  snap.suspended = snap_suspended_;
  snap.has_tradability = true;
  cache_[code_full] = snap;
  return snap_open_;
}

QuoteSnapshot QuoteConnection::GetSnapshot(const std::string &code_full) const {
  std::lock_guard<std::mutex> lk(mu_);
  std::map<std::string, QuoteSnapshot>::const_iterator it = cache_.find(code_full);
  if (it == cache_.end()) {
    return QuoteSnapshot();
  }
  return it->second;
}

Qot_Common::QotMarket QuoteConnection::ToSdkMarketSymbol(
    const quote::QuoteSymbol &sym) const {
  if (sym.prefix == "HK") {
    return Qot_Common::QotMarket_HK_Security;
  }
  return Qot_Common::QotMarket_US_Security;
}

Qot_Common::KLType QuoteConnection::ToSdkKlType(quote::KlTypeKey key) const {
  switch (key) {
    case quote::KlTypeKey::k1M:
      return Qot_Common::KLType_1Min;
    case quote::KlTypeKey::k3M:
      return Qot_Common::KLType_3Min;
    case quote::KlTypeKey::k5M:
      return Qot_Common::KLType_5Min;
    case quote::KlTypeKey::k15M:
      return Qot_Common::KLType_15Min;
    case quote::KlTypeKey::k30M:
      return Qot_Common::KLType_30Min;
    case quote::KlTypeKey::k60M:
      return Qot_Common::KLType_60Min;
    case quote::KlTypeKey::kDay:
      return Qot_Common::KLType_Day;
    case quote::KlTypeKey::kWeek:
      return Qot_Common::KLType_Week;
    case quote::KlTypeKey::kMon:
      return Qot_Common::KLType_Month;
    case quote::KlTypeKey::kQuarter:
      return Qot_Common::KLType_Quarter;
    case quote::KlTypeKey::kYear:
      return Qot_Common::KLType_Year;
  }
  return Qot_Common::KLType_5Min;
}

Qot_Common::SubType QuoteConnection::ToSdkSubType(quote::KlTypeKey key) const {
  switch (key) {
    case quote::KlTypeKey::k1M:
      return Qot_Common::SubType_KL_1Min;
    case quote::KlTypeKey::k3M:
      return Qot_Common::SubType_KL_3Min;
    case quote::KlTypeKey::k5M:
      return Qot_Common::SubType_KL_5Min;
    case quote::KlTypeKey::k15M:
      return Qot_Common::SubType_KL_15Min;
    case quote::KlTypeKey::k30M:
      return Qot_Common::SubType_KL_30Min;
    case quote::KlTypeKey::k60M:
      return Qot_Common::SubType_KL_60Min;
    case quote::KlTypeKey::kDay:
      return Qot_Common::SubType_KL_Day;
    case quote::KlTypeKey::kWeek:
      return Qot_Common::SubType_KL_Week;
    case quote::KlTypeKey::kMon:
      return Qot_Common::SubType_KL_Month;
    case quote::KlTypeKey::kQuarter:
      return Qot_Common::SubType_KL_Qurater;
    case quote::KlTypeKey::kYear:
      return Qot_Common::SubType_KL_Year;
  }
  return Qot_Common::SubType_KL_5Min;
}

KlineFetchResult QuoteConnection::FetchHistoricalKlines(
    const std::string &code_full, quote::KlTypeKey kl_type, int req_num,
    bool extended_time) {
  KlineFetchResult out;
  if (!connected_) {
    out.error = "quote not connected";
    return out;
  }

  const quote::QuoteSymbol sym = quote::ParseQuoteSymbol(code_full);
  const Qot_Common::QotMarket market = ToSdkMarketSymbol(sym);
  const Qot_Common::KLType sdk_kl = ToSdkKlType(kl_type);
  const Qot_Common::SubType sdk_sub = ToSdkSubType(kl_type);

  Qot_Sub::Request sub_req;
  Qot_Sub::C2S *sub_c2s = sub_req.mutable_c2s();
  Qot_Common::Security *sec = sub_c2s->add_securitylist();
  sec->set_market(market);
  sec->set_code(sym.code);
  sub_c2s->add_subtypelist(sdk_sub);
  sub_c2s->set_issuborunsub(true);
  sub_c2s->set_isregorunregpush(false);
  sub_c2s->set_extendedtime(extended_time);
  WaitReply(m_pQotApi->Sub(sub_req));

  kline_get_ok_ = false;
  kline_last_bars_.clear();
  Qot_GetKL::Request get_req;
  Qot_GetKL::C2S *get_c2s = get_req.mutable_c2s();
  get_c2s->set_rehabtype(Qot_Common::RehabType_Forward);
  get_c2s->set_kltype(sdk_kl);
  Qot_Common::Security *gsec = get_c2s->mutable_security();
  gsec->set_market(market);
  gsec->set_code(sym.code);
  get_c2s->set_reqnum(req_num);
  WaitReply(m_pQotApi->GetKL(get_req));

  out.ok = kline_get_ok_;
  out.ret_code = out.ok ? 0 : -1;
  out.error = kline_last_error_;
  out.bars = kline_last_bars_;
  return out;
}

void QuoteConnection::OnReply_Sub(Moomoo::u32_t, const Qot_Sub::Response &stRsp) {
  sub_ok_ = (stRsp.rettype() == Common::RetType_Succeed);
  if (!sub_ok_) {
    PrintUTF8(stRsp.retmsg());
  }
  PostReply();
}

void QuoteConnection::OnReply_GetOrderBook(Moomoo::u32_t, const Qot_GetOrderBook::Response &stRsp) {
  get_ok_ = (stRsp.rettype() == Common::RetType_Succeed);
  if (get_ok_ && stRsp.has_s2c()) {
    std::lock_guard<std::mutex> lk(mu_);
    ApplyBookToCache(stRsp.s2c().security(), stRsp.s2c().orderbookbidlist(),
                     stRsp.s2c().orderbookasklist(), 0, &cache_);
  } else if (!get_ok_) {
    PrintUTF8(stRsp.retmsg());
  }
  PostReply();
}

void QuoteConnection::OnReply_GetSecuritySnapshot(Moomoo::u32_t,
                                                  const Qot_GetSecuritySnapshot::Response &stRsp) {
  snap_ok_ = (stRsp.rettype() == Common::RetType_Succeed);
  snap_open_ = 0.0;
  snap_lot_size_ = 0;
  snap_last_price_ = 0.0;
  snap_sec_status_ = 0;
  snap_suspended_ = false;
  if (snap_ok_ && stRsp.has_s2c() && stRsp.s2c().snapshotlist_size() > 0) {
    const Qot_GetSecuritySnapshot::Snapshot &snap = stRsp.s2c().snapshotlist(0);
    if (snap.has_basic()) {
      snap_open_ = snap.basic().openprice();
      snap_lot_size_ = snap.basic().lotsize();
      snap_last_price_ = snap.basic().curprice();
      if (snap.basic().has_issuspend()) {
        snap_suspended_ = snap.basic().issuspend();
      }
      if (snap.basic().has_secstatus()) {
        snap_sec_status_ = snap.basic().secstatus();
      }
    }
  } else if (!snap_ok_) {
    PrintUTF8(stRsp.retmsg());
  }
  PostReply();
}

void QuoteConnection::OnReply_GetKL(Moomoo::u32_t, const Qot_GetKL::Response &stRsp) {
  kline_get_ok_ = (stRsp.rettype() == Common::RetType_Succeed);
  kline_last_bars_.clear();
  if (kline_get_ok_) {
    const Qot_GetKL::S2C &s2c = stRsp.s2c();
    for (int i = 0; i < s2c.kllist_size(); ++i) {
      const Qot_Common::KLine &kl = s2c.kllist(i);
      KlineBar bar;
      bar.time_key = kl.time();
      bar.timestamp_s = kl.timestamp();
      bar.open = kl.openprice();
      bar.high = kl.highprice();
      bar.low = kl.lowprice();
      bar.close = kl.closeprice();
      bar.volume = kl.volume();
      bar.turnover = kl.turnover();
      kline_last_bars_.push_back(bar);
    }
  } else {
    kline_last_error_ = stRsp.retmsg();
    PrintUTF8(stRsp.retmsg());
  }
  PostReply();
}

void QuoteConnection::NotifyBookPush() {
  {
    std::lock_guard<std::mutex> lk(book_wait_mu_);
    ++book_push_seq_;
  }
  book_wait_cv_.notify_all();
}

bool QuoteConnection::WaitBookPush(double timeout_sec) {
  const double t = timeout_sec > 0.0 ? timeout_sec : 0.5;
  std::unique_lock<std::mutex> lk(book_wait_mu_);
  const std::uint64_t seq = book_push_seq_;
  book_wait_cv_.wait_for(lk, std::chrono::milliseconds(static_cast<int>(t * 1000.0)),
                         [this, seq]() { return book_push_seq_ != seq || !connected_; });
  return book_push_seq_ != seq;
}

void QuoteConnection::OnPush_UpdateOrderBook(const Qot_UpdateOrderBook::Response &stRsp) {
  if (!stRsp.has_s2c()) {
    return;
  }
  {
    std::lock_guard<std::mutex> lk(mu_);
    const auto& book = stRsp.s2c();
    const std::int64_t exchange_ts_ms = std::max(
        book.has_svrrecvtimebidtimestamp()
            ? OpenDServerTimestampMs(book.svrrecvtimebidtimestamp()) : 0,
        book.has_svrrecvtimeasktimestamp()
            ? OpenDServerTimestampMs(book.svrrecvtimeasktimestamp()) : 0);
    ApplyBookToCache(stRsp.s2c().security(), stRsp.s2c().orderbookbidlist(),
                     stRsp.s2c().orderbookasklist(), exchange_ts_ms, &cache_);
  }
  NotifyBookPush();
}

}  // namespace opend
}  // namespace mmtrade
