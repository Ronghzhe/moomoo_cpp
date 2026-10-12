#include "opend/trade_connection.hpp"

#include "opend/connect_guard.hpp"
#include "opend/symbol_util.hpp"
#include "trade/order_id_util.hpp"

#include <chrono>
#include <cstdlib>
#include <set>
#include <sstream>
#include <string>
#include <thread>

namespace mmtrade {
namespace opend {

TradeConnection::TradeConnection()
    : connected_(false),
      simulate_(true),
      push_attached_(false),
      sub_acc_push_ok_(false),
      place_ok_(false),
      query_ok_(false),
      modify_ok_(false),
      funds_ok_(false),
      positions_ok_(false),
      last_order_id_(0),
      prefer_acc_id_(0) {}

void TradeConnection::EnsureTrdApi() {
  if (m_pTrdApi != nullptr) {
    return;
  }
  m_pTrdApi = MMAPI::CreateTrdApi();
  if (m_pTrdApi == nullptr) {
    return;
  }
  m_pTrdApi->RegisterTrdSpi(this);
  m_pTrdApi->RegisterConnSpi(this);
  m_bTrdInitSuc = false;
}

bool TradeConnection::Connect(const std::string &host, int port, const std::string &unlock_pwd_md5) {
  if (trade_connected()) {
    return true;
  }
  if (connected_) {
    Disconnect();
  }
  EnsureTrdApi();
  {
    OpenDConnectGuard guard;
    if (!InitTrd(host.c_str(), static_cast<uint16_t>(port), unlock_pwd_md5.c_str())) {
      last_error_ = "InitTrd failed";
      connected_ = false;
      return false;
    }
  }
  connected_ = true;
  MaybeSleepAfterOpenDConnect();
  return true;
}

void TradeConnection::Disconnect() {
  if (connected_) {
    UnInitTrd();
    connected_ = false;
    push_attached_ = false;
    sub_acc_push_ok_ = false;
  }
}

bool TradeConnection::EnsureAccPush(std::uint64_t prefer_acc_id) {
  if (m_pTrdApi == NULL) {
    last_error_ = "trade api null";
    return false;
  }
  m_pTrdApi->RegisterTrdSpi(this);
  m_pTrdApi->RegisterConnSpi(this);

  Trd_SubAccPush::Request req;
  Trd_SubAccPush::C2S *c2s = req.mutable_c2s();
  if (!m_setAllAccID.empty()) {
    for (std::set<uint64_t>::const_iterator it = m_setAllAccID.begin(); it != m_setAllAccID.end();
         ++it) {
      c2s->add_accidlist(*it);
    }
  } else if (prefer_acc_id != 0) {
    c2s->add_accidlist(prefer_acc_id);
  } else {
    last_error_ = "no acc id for SubAccPush";
    return false;
  }
  sub_acc_push_ok_ = false;
  WaitReply(m_pTrdApi->SubAccPush(req));
  push_attached_ = sub_acc_push_ok_;
  return sub_acc_push_ok_;
}

std::uint64_t TradeConnection::ResolveAccId(const std::string &code_full) {
  if (prefer_acc_id_ != 0) {
    return prefer_acc_id_;
  }
  const SdkMarketInfo market = ParseMarket(code_full);
  const Trd_Common::TrdEnv env = TrdEnvFromSimulate(simulate_);
  return GetFirstAccID(market.trd_market, env);
}

Trd_Common::TrdSide TradeConnection::ToSdkSide(trade::TrdSide side) const {
  return side == trade::TrdSide::kBuy ? Trd_Common::TrdSide_Buy : Trd_Common::TrdSide_Sell;
}

Trd_Common::OrderType TradeConnection::ToSdkOrderType(trade::OrderType ot) const {
  return ot == trade::OrderType::kMarket ? Trd_Common::OrderType_Market
                                         : Trd_Common::OrderType_Normal;
}

Trd_Common::TimeInForce TradeConnection::ToSdkTif(trade::TimeInForce tif) const {
  return tif == trade::TimeInForce::kGtc ? Trd_Common::TimeInForce_GTC : Trd_Common::TimeInForce_DAY;
}

namespace {

std::int64_t TimestampToMs(double ts)
{
  if (ts <= 0.0)
  {
    return 0;
  }
  if (ts > 1e14)
  {
    return static_cast<std::int64_t>(ts / 1000.0);
  }
  if (ts > 1e11)
  {
    return static_cast<std::int64_t>(ts);
  }
  return static_cast<std::int64_t>(ts * 1000.0);
}

std::int64_t OrderCreateTsMs(const Trd_Common::Order &ord)
{
  return ord.has_createtimestamp() ? TimestampToMs(ord.createtimestamp()) : 0;
}

std::int64_t OrderFillTsMs(const Trd_Common::Order &ord)
{
  if (ord.fillqty() <= 1e-9)
  {
    return 0;
  }
  return ord.has_updatetimestamp() ? TimestampToMs(ord.updatetimestamp()) : 0;
}

}  // namespace

void TradeConnection::IngestOrderPush(std::uint64_t order_id, const std::string &order_id_ex,
                                      const std::string &status, double dealt_qty,
                                      double dealt_avg_price, const std::string &code,
                                      std::int64_t create_ts_ms, std::int64_t fill_ts_ms) {
  const std::string cache_key = trade::ResolveDisplayOrderId(order_id, order_id_ex);
  if (cache_key.empty()) {
    return;
  }
  trade::OrderPushRow row;
  row.order_id = cache_key;
  row.order_status = status;
  row.code = code;
  row.numeric_order_id = order_id;
  row.dealt_qty = dealt_qty;
  row.dealt_avg_price = dealt_avg_price;
  row.create_ts_ms = create_ts_ms;
  row.fill_ts_ms = fill_ts_ms;
  push_cache_.Ingest(row);
}

PlaceOrderResult TradeConnection::Place(const PlaceOrderParams &params) {
  PlaceOrderResult out;
  if (!trade_connected()) {
    out.error = "not connected";
    return out;
  }

  const auto wall_us = []() -> int64_t {
    const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
  };
  const int64_t t0 = wall_us();

  const std::string code = ParseCode(params.code_full);
  const SdkMarketInfo market = ParseMarket(params.code_full);
  std::uint64_t acc_id = ResolveAccId(params.code_full);
  if (acc_id == 0) {
    out.error = std::string("no simulate account for ") + market.name;
    return out;
  }
  // 启动阶段已完成订单推送订阅。只在需要首次订阅时保留稳定性等待，
  // 不能让每一笔热路径订单额外等待 200ms。
  const bool push_was_attached = push_attached_;
  if (!push_was_attached && !EnsureAccPush(acc_id)) {
    out.error = "OpenD account-push subscription failed";
    return out;
  }
  if (!push_was_attached && !SkipOpenDConnectSleep()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  place_ok_ = false;
  last_order_id_ = 0;
  last_order_id_ex_.clear();
  last_error_.clear();
  last_place_response_raw_.clear();

  Trd_PlaceOrder::Request place_req;
  Trd_PlaceOrder::C2S *place_c2s = place_req.mutable_c2s();
  Trd_Common::TrdHeader *hdr = place_c2s->mutable_header();
  hdr->set_trdenv(TrdEnvFromSimulate(simulate_));
  hdr->set_accid(acc_id);
  hdr->set_trdmarket(market.trd_market);
  place_c2s->set_trdside(ToSdkSide(params.side));
  place_c2s->set_ordertype(ToSdkOrderType(params.order_type));
  place_c2s->set_code(code);
  place_c2s->set_qty(params.qty);
  place_c2s->set_price(params.price);
  place_c2s->set_secmarket(market.sec_market);
  place_c2s->set_remark(params.remark.empty() ? "moomoo_trade" : params.remark);
  place_c2s->set_timeinforce(ToSdkTif(params.time_in_force));
  place_c2s->set_filloutsiderth(params.fill_outside_rth);

  const int64_t t1 = wall_us();
  const int place_serial = m_pTrdApi->PlaceOrder(place_req);
  if (place_serial == 0) {
    const int64_t t2 = wall_us();
    out.pre_sdk_us = t1 - t0;
    out.sdk_us = t2 - t1;
    out.post_sdk_us = 0;
    out.ret_code = -1;
    out.error = "OpenD SDK PlaceOrder returned serial=0; request was not sent";
    out.raw_response = "sdk_place_order_serial=0; no OpenD callback received";
    out.response_source = "sdk_send_failure";
    return out;
  }
  WaitReply(place_serial);
  const int64_t t2 = wall_us();
  out.pre_sdk_us = t1 - t0;
  out.sdk_us = t2 - t1;
  out.ok = place_ok_ && (last_order_id_ != 0 || !last_order_id_ex_.empty());
  out.ret_code = out.ok ? 0 : -1;
  out.order_id = last_order_id_;
  out.order_id_ex = last_order_id_ex_;
  out.error = last_error_;
  out.raw_response = last_place_response_raw_;
  out.response_source = "submit_response";
  if (!out.ok && out.error.empty()) {
    out.error = place_ok_ ? "OpenD returned success without orderId/orderIdEx"
                          : "OpenD place-order reply failed without retMsg";
  }
  if (out.raw_response.empty()) {
    out.raw_response = "sdk_place_order_serial=" + std::to_string(place_serial) +
                       "; no OpenD callback payload captured";
  }
  if (out.ok) {
    IngestOrderPush(last_order_id_, last_order_id_ex_, "SUBMITTING", 0.0, 0.0, code);
  }
  const int64_t t3 = wall_us();
  out.post_sdk_us = t3 - t2;
  return out;
}

bool TradeConnection::QueryOrder(std::uint64_t order_id, const std::string &code_full) {
  if (!trade_connected() || order_id == 0) {
    return false;
  }
  const std::string code = ParseCode(code_full);
  const SdkMarketInfo market = ParseMarket(code_full);
  const std::uint64_t acc_id = ResolveAccId(code_full);
  if (acc_id == 0) {
    return false;
  }

  query_ok_ = false;
  Trd_GetOrderList::Request list_req;
  Trd_GetOrderList::C2S *list_c2s = list_req.mutable_c2s();
  Trd_Common::TrdHeader *lhdr = list_c2s->mutable_header();
  lhdr->set_trdenv(TrdEnvFromSimulate(simulate_));
  lhdr->set_accid(acc_id);
  lhdr->set_trdmarket(market.trd_market);
  list_c2s->mutable_filterconditions()->add_codelist(code);
  list_c2s->mutable_filterconditions()->add_idlist(order_id);
  list_c2s->set_refreshcache(true);
  WaitReply(m_pTrdApi->GetOrderList(list_req));
  return query_ok_;
}

bool TradeConnection::CancelOrder(std::uint64_t order_id, const std::string &code_full) {
  if (!trade_connected() || order_id == 0) {
    return false;
  }
  const SdkMarketInfo market = ParseMarket(code_full);
  const std::uint64_t acc_id = ResolveAccId(code_full);
  if (acc_id == 0) {
    return false;
  }

  modify_ok_ = false;
  Trd_ModifyOrder::Request mod_req;
  Trd_ModifyOrder::C2S *mod_c2s = mod_req.mutable_c2s();
  Trd_Common::TrdHeader *mhdr = mod_c2s->mutable_header();
  mhdr->set_trdenv(TrdEnvFromSimulate(simulate_));
  mhdr->set_accid(acc_id);
  mhdr->set_trdmarket(market.trd_market);
  mod_c2s->set_orderid(order_id);
  mod_c2s->set_modifyorderop(Trd_Common::ModifyOrderOp_Cancel);
  mod_c2s->set_qty(0);
  mod_c2s->set_price(0);
  WaitReply(m_pTrdApi->ModifyOrder(mod_req));
  return modify_ok_;
}

AccountList TradeConnection::GetAccountListCached() const {
  AccountList out;
  for (std::vector<std::pair<std::uint64_t, std::set<int32_t> > >::const_iterator it =
           m_vSimAccID.begin();
       it != m_vSimAccID.end(); ++it) {
    AccountInfo row;
    row.acc_id = it->first;
    row.simulate = true;
    out.push_back(row);
  }
  for (std::vector<std::pair<std::uint64_t, std::set<int32_t> > >::const_iterator it =
           m_vRealAccID.begin();
       it != m_vRealAccID.end(); ++it) {
    AccountInfo row;
    row.acc_id = it->first;
    row.simulate = false;
    out.push_back(row);
  }
  return out;
}

bool TradeConnection::GetFunds(const std::string &code_full, FundsInfo *out) {
  if (!trade_connected() || out == NULL) {
    return false;
  }
  const SdkMarketInfo market = ParseMarket(code_full);
  const std::uint64_t acc_id = ResolveAccId(code_full);
  if (acc_id == 0) {
    last_error_ = std::string("no account for ") + market.name;
    return false;
  }

  funds_ok_ = false;
  Trd_GetFunds::Request req;
  Trd_GetFunds::C2S *c2s = req.mutable_c2s();
  Trd_Common::TrdHeader *hdr = c2s->mutable_header();
  hdr->set_trdenv(TrdEnvFromSimulate(simulate_));
  hdr->set_accid(acc_id);
  hdr->set_trdmarket(market.trd_market);
  c2s->set_refreshcache(true);
  c2s->set_currency(market.trd_market == Trd_Common::TrdMarket_US ? Trd_Common::Currency_USD
                                                                    : Trd_Common::Currency_HKD);
  WaitReply(m_pTrdApi->GetFunds(req));
  if (funds_ok_) {
    *out = last_funds_;
  }
  return funds_ok_;
}

bool TradeConnection::GetPositions(const std::string &code_full, const std::string &filter_code,
                                     PositionList *out) {
  if (!trade_connected() || out == NULL) {
    return false;
  }
  const SdkMarketInfo market = ParseMarket(code_full);
  const std::uint64_t acc_id = ResolveAccId(code_full);
  if (acc_id == 0) {
    last_error_ = std::string("no account for ") + market.name;
    return false;
  }

  positions_ok_ = false;
  last_positions_.clear();
  Trd_GetPositionList::Request req;
  Trd_GetPositionList::C2S *c2s = req.mutable_c2s();
  Trd_Common::TrdHeader *hdr = c2s->mutable_header();
  hdr->set_trdenv(TrdEnvFromSimulate(simulate_));
  hdr->set_accid(acc_id);
  hdr->set_trdmarket(market.trd_market);
  if (!filter_code.empty()) {
    c2s->mutable_filterconditions()->add_codelist(ParseCode(filter_code));
  }
  c2s->set_refreshcache(true);
  WaitReply(m_pTrdApi->GetPositionList(req));
  if (positions_ok_) {
    *out = last_positions_;
  }
  return positions_ok_;
}

void TradeConnection::OnReply_GetFunds(Moomoo::u32_t, const Trd_GetFunds::Response &stRsp) {
  funds_ok_ = (stRsp.rettype() == Common::RetType_Succeed);
  if (funds_ok_ && stRsp.has_s2c() && stRsp.s2c().has_funds()) {
    const Trd_Common::Funds &f = stRsp.s2c().funds();
    last_funds_.cash = f.cash();
    last_funds_.avl_withdrawal_cash = f.avlwithdrawalcash();
    last_funds_.power = f.power();
    last_funds_.total_assets = f.totalassets();
    last_funds_.market_val = f.marketval();
    last_funds_.currency = f.has_currency() ? std::to_string(f.currency()) : "";
  } else if (!funds_ok_) {
    last_error_ = stRsp.retmsg();
    PrintUTF8(stRsp.retmsg());
  }
  PostReply();
}

void TradeConnection::OnReply_GetPositionList(Moomoo::u32_t,
                                              const Trd_GetPositionList::Response &stRsp) {
  positions_ok_ = (stRsp.rettype() == Common::RetType_Succeed);
  last_positions_.clear();
  if (positions_ok_) {
    const Trd_GetPositionList::S2C &s2c = stRsp.s2c();
    for (int i = 0; i < s2c.positionlist_size(); ++i) {
      const Trd_Common::Position &p = s2c.positionlist(i);
      PositionInfo row;
      row.code = p.code();
      row.qty = p.qty();
      row.can_sell_qty = p.cansellqty();
      row.cost_price = p.costprice();
      row.market_val = p.val();
      row.pl_val = p.plval();
      if (p.has_positionside()) {
        if (p.positionside() == Trd_Common::PositionSide_Short) {
          row.position_side = "SHORT";
        } else if (p.positionside() == Trd_Common::PositionSide_Long) {
          row.position_side = "LONG";
        }
      }
      last_positions_.push_back(row);
    }
  } else {
    last_error_ = stRsp.retmsg();
    PrintUTF8(stRsp.retmsg());
  }
  PostReply();
}

void TradeConnection::OnReply_PlaceOrder(Moomoo::u32_t, const Trd_PlaceOrder::Response &stRsp) {
  last_place_response_raw_ = stRsp.DebugString();
  place_ok_ = (stRsp.rettype() == Common::RetType_Succeed);
  if (place_ok_ && stRsp.has_s2c()) {
    const Trd_PlaceOrder::S2C &s2c = stRsp.s2c();
    if (s2c.has_orderid()) {
      last_order_id_ = s2c.orderid();
    }
    if (s2c.has_orderidex()) {
      last_order_id_ex_ = s2c.orderidex();
    }
  } else if (!place_ok_) {
    last_error_ = stRsp.retmsg();
    PrintUTF8(stRsp.retmsg());
  }
  PostReply();
}

void TradeConnection::OnReply_GetOrderList(Moomoo::u32_t, const Trd_GetOrderList::Response &stRsp) {
  query_ok_ = (stRsp.rettype() == Common::RetType_Succeed);
  if (query_ok_ && stRsp.has_s2c()) {
    const Trd_GetOrderList::S2C &s2c = stRsp.s2c();
    for (int i = 0; i < s2c.orderlist_size(); ++i) {
      const Trd_Common::Order &ord = s2c.orderlist(i);
      std::ostringstream status;
      status << ord.orderstatus();
      IngestOrderPush(ord.orderid(), ord.orderidex(), status.str(), ord.fillqty(),
                      ord.fillavgprice(), ord.code(), OrderCreateTsMs(ord), OrderFillTsMs(ord));
    }
  } else if (!query_ok_) {
    last_error_ = stRsp.retmsg();
    PrintUTF8(stRsp.retmsg());
  }
  PostReply();
}

void TradeConnection::OnReply_ModifyOrder(Moomoo::u32_t, const Trd_ModifyOrder::Response &stRsp) {
  modify_ok_ = (stRsp.rettype() == Common::RetType_Succeed);
  if (!modify_ok_) {
    last_error_ = stRsp.retmsg();
    PrintUTF8(stRsp.retmsg());
  }
  PostReply();
}

void TradeConnection::OnReply_SubAccPush(Moomoo::u32_t, const Trd_SubAccPush::Response &stRsp) {
  sub_acc_push_ok_ = (stRsp.rettype() == Common::RetType_Succeed);
  if (!sub_acc_push_ok_) {
    PrintUTF8(stRsp.retmsg());
  }
  PostReply();
}

void TradeConnection::OnPush_UpdateOrder(const Trd_UpdateOrder::Response &stRsp) {
  if (!stRsp.has_s2c()) {
    return;
  }
  const Trd_Common::Order &ord = stRsp.s2c().order();
  std::ostringstream status;
  status << ord.orderstatus();
  IngestOrderPush(ord.orderid(), ord.orderidex(), status.str(), ord.fillqty(), ord.fillavgprice(),
                  ord.code(), OrderCreateTsMs(ord), OrderFillTsMs(ord));
}

void TradeConnection::OnPush_UpdateOrderFill(const Trd_UpdateOrderFill::Response &stRsp) {
  if (!stRsp.has_s2c() || !stRsp.s2c().has_orderfill()) {
    return;
  }
  const Trd_Common::OrderFill &fill = stRsp.s2c().orderfill();
  const std::int64_t fill_ts_ms =
      fill.has_createtimestamp() ? TimestampToMs(fill.createtimestamp()) : 0;
  IngestOrderPush(fill.orderid(), fill.orderidex(), "FILL", fill.qty(), fill.price(), fill.code(), 0,
                  fill_ts_ms);
}

void TradeConnection::OnDisConnect(MMAPI_Conn *pConn, Moomoo::i64_t) {
  if (pConn == m_pTrdApi) {
    connected_ = false;
    push_attached_ = false;
    sub_acc_push_ok_ = false;
    m_bTrdInitSuc = false;
  }
}

}  // namespace opend
}  // namespace mmtrade
