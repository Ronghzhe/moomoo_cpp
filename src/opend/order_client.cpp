#include "opend/order_client.hpp"

#include "opend/account_types.hpp"
#include "trade/order_id_util.hpp"
#include "trade/trade_pwd_util.hpp"

#include <cstdlib>
#include <ctime>
#include <sstream>

namespace mmtrade {
namespace opend {

OrderClient::OrderClient(const OrderConfig &config, TradeConnection *shared_conn)
    : config_(config), conn_(shared_conn), push_ready_(false), place_broker_(NULL) {
  if (conn_ == NULL) {
    owned_conn_.reset(new TradeConnection());
    conn_ = owned_conn_.get();
  }
}

void OrderClient::SetPlaceBroker(trade::IOrderBroker *broker) { place_broker_ = broker; }

OrderClient::~OrderClient() { Close(); }

bool OrderClient::trade_connected() const { return conn_ != NULL && conn_->trade_connected(); }

bool OrderClient::Connect() {
  if (conn_ == NULL) {
    owned_conn_.reset(new TradeConnection());
    conn_ = owned_conn_.get();
  }
  conn_->set_simulate(config_.simulate);
  if (config_.acc_id != 0) {
    conn_->set_prefer_acc_id(config_.acc_id);
  }
  if (!trade_connected()) {
    push_ready_ = false;
  }
  std::string unlock_md5;
  if (!config_.simulate) {
    const std::string pwd = trade::ResolveTradePasswordPlain();
    if (pwd.empty()) {
      return false;
    }
    unlock_md5 = trade::Md5HexLower(pwd);
  }
  return conn_->Connect(config_.host, config_.port, unlock_md5);
}

void OrderClient::Close() {
  if (owned_conn_.get() != NULL && conn_ != NULL) {
    conn_->Disconnect();
  }
  push_ready_ = false;
}

trade::OrderPushCache &OrderClient::EnsurePushHub() {
  EnsurePushSubscription();
  return conn_->PushCache();
}

bool OrderClient::EnsurePushSubscription() {
  if (conn_ == NULL) {
    owned_conn_.reset(new TradeConnection());
    conn_ = owned_conn_.get();
  }
  if (!push_ready_) {
    const std::uint64_t acc = config_.acc_id != 0 ? config_.acc_id : conn_->ResolveAccId("US.AAPL");
    push_ready_ = conn_->EnsureAccPush(acc);
  }
  return push_ready_;
}

const trade::OrderPushCache *OrderClient::PushHub() const {
  if (conn_ == NULL) {
    return NULL;
  }
  return &conn_->PushCache();
}

PlaceOrderResult OrderClient::Place(const std::string &code_full, const std::string &side,
                                    double qty, double price, const std::string &order_type,
                                    const std::string &remark, const std::string &time_in_force,
                                    bool fill_outside_rth) {
  EnsurePushHub();
  if (place_broker_ != NULL) {
    trade::PlaceOrderRequest req;
    req.code = code_full;
    req.side = side;
    req.qty = qty;
    req.price = price;
    req.order_type = order_type;
    req.remark = remark;
    req.time_in_force = time_in_force;
    req.fill_outside_rth = fill_outside_rth;
    {
      std::ostringstream oss;
      oss << "cpp-" << code_full << "-" << static_cast<long long>(::time(NULL)) << "-"
          << std::rand();
      req.request_id = oss.str();
    }
    req.context.symbol = code_full;
    req.context.opend_host = config_.host;
    req.context.opend_port = config_.port;
    req.context.trd_env = config_.simulate ? "SIMULATE" : "REAL";
    req.context.trd_market = trade::TrdMarketName(config_.trd_market);
    req.context.acc_id = std::to_string(static_cast<unsigned long long>(config_.acc_id));
    const trade::PlaceOrderResult br = place_broker_->PlaceOrder(req);
    PlaceOrderResult out;
    out.ok = br.ok;
    out.ret_code = br.ret_code;
    out.error = br.error;
    out.order_id = br.internal_order_id != 0 ? br.internal_order_id
                                            : trade::ParseOrderIdUint64(br.order_id);
    out.order_id_ex = br.order_id;
    out.pre_sdk_us = br.place_pre_us;
    out.sdk_us = br.place_sdk_us;
    out.post_sdk_us = br.place_post_us;
    out.gateway_queue_wait_us = br.queue_wait_us;
    out.gateway_rate_wait_us = br.rate_wait_us;
    out.gateway_http_rtt_us = br.http_rtt_us;
    out.place_via = br.place_via.empty() ? "broker" : br.place_via;
    return out;
  }
  PlaceOrderParams p;
  p.code_full = code_full;
  p.side = trade::ParseTrdSide(side);
  p.qty = qty;
  p.price = price;
  p.order_type = trade::ParseOrderType(order_type);
  p.remark = remark;
  p.time_in_force = trade::ParseTimeInForce(time_in_force);
  p.fill_outside_rth = fill_outside_rth;
  PlaceOrderResult out = conn_->Place(p);
  if (out.place_via.empty()) {
    out.place_via = "direct";
  }
  return out;
}

bool OrderClient::CancelOrder(std::uint64_t order_id, const std::string &code_full) {
  if (conn_ == NULL) {
    return false;
  }
  return conn_->CancelOrder(order_id, code_full);
}

bool OrderClient::QueryOrder(std::uint64_t order_id, const std::string &code_full) {
  if (conn_ == NULL) {
    return false;
  }
  return conn_->QueryOrder(order_id, code_full);
}

bool OrderClient::GetPositions(const std::string &code_full, PositionList *out) {
  if (conn_ == NULL || out == NULL) {
    return false;
  }
  return conn_->GetPositions(code_full, code_full, out);
}

}  // namespace opend
}  // namespace mmtrade
