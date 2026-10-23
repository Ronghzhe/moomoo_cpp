#include "opend/direct_broker.hpp"

#include "opend/order_client.hpp"
#include "opend/trade_connection.hpp"
#include "trade/broker_mapping.hpp"
#include "trade/order_parse.hpp"

namespace mmtrade {
namespace opend {

namespace {

OrderConfig ToOrderConfig(const trade::DirectBrokerConfig &cfg) {
  OrderConfig out;
  out.host = cfg.host;
  out.port = cfg.port;
  out.trd_market = cfg.trd_market;
  out.simulate = cfg.simulate;
  out.acc_id = cfg.acc_id;
  out.refresh_cache = cfg.refresh_cache;
  return out;
}

}  // namespace

PlaceOrderParams ParamsFromBrokerRequest(const trade::PlaceOrderRequest &req) {
  PlaceOrderParams p;
  p.code_full = req.code;
  p.side = trade::ParseTrdSide(req.side);
  p.qty = req.qty;
  p.price = req.price;
  p.order_type = trade::ParseOrderType(req.order_type);
  p.remark = req.remark.empty() ? "direct_broker" : req.remark;
  p.time_in_force = trade::ParseTimeInForce(req.time_in_force);
  p.fill_outside_rth = req.fill_outside_rth;
  p.sdk_session = req.session;
  return p;
}

DirectBroker::DirectBroker(const trade::DirectBrokerConfig &config, TradeConnection *shared_trade)
    : config_(config), shared_trade_(shared_trade), connected_(false) {
  if (shared_trade_ == NULL) {
    client_.reset(new OrderClient(ToOrderConfig(config)));
  }
}

DirectBroker::~DirectBroker() { Close(); }

OrderClient *DirectBroker::Client() { return client_.get(); }

trade::PlaceOrderResult DirectBroker::PlaceOrder(const trade::PlaceOrderRequest &req) {
  PlaceOrderResult raw;
  if (shared_trade_ != NULL && shared_trade_->trade_connected()) {
    raw = shared_trade_->Place(ParamsFromBrokerRequest(req));
  } else {
    if (client_.get() == NULL) {
      return trade::MakeBrokerFailure(-1, "client not initialized");
    }
    if (!connected_) {
      connected_ = client_->Connect();
      if (!connected_) {
        return trade::MakeBrokerFailure(-1, "connect failed");
      }
    }
    raw = client_->Place(req.code, req.side, req.qty, req.price, req.order_type,
                         req.remark.empty() ? "direct_broker" : req.remark, req.time_in_force,
                         req.fill_outside_rth, req.session);
  }
  trade::OpenDPlaceSnapshot snap;
  snap.ok = raw.ok;
  snap.ret_code = raw.ret_code;
  snap.order_id = raw.order_id;
  snap.order_id_ex = raw.order_id_ex;
  snap.error = raw.error;
  trade::PlaceOrderResult out = trade::MapPlaceSnapshot(snap);
  out.place_pre_us = raw.pre_sdk_us;
  out.place_sdk_us = raw.sdk_us;
  out.place_post_us = raw.post_sdk_us;
  if (out.place_via.empty()) {
    out.place_via = "direct";
  }
  return out;
}

void DirectBroker::Close() {
  if (client_.get() != NULL) {
    client_->Close();
  }
  shared_trade_ = NULL;
  connected_ = false;
}

}  // namespace opend
}  // namespace mmtrade
