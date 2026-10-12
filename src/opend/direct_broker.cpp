#include "opend/direct_broker.hpp"

#include "opend/order_client.hpp"
#include "trade/broker_mapping.hpp"

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

DirectBroker::DirectBroker(const trade::DirectBrokerConfig &config)
    : config_(config), connected_(false) {
  client_.reset(new OrderClient(ToOrderConfig(config)));
}

DirectBroker::~DirectBroker() { Close(); }

OrderClient *DirectBroker::Client() { return client_.get(); }

trade::PlaceOrderResult DirectBroker::PlaceOrder(const trade::PlaceOrderRequest &req) {
  if (client_.get() == NULL) {
    return trade::MakeBrokerFailure(-1, "client not initialized");
  }
  if (!connected_) {
    connected_ = client_->Connect();
    if (!connected_) {
      return trade::MakeBrokerFailure(-1, "connect failed");
    }
  }
  const PlaceOrderResult raw = client_->Place(
      req.code, req.side, req.qty, req.price, req.order_type,
      req.remark.empty() ? "direct_broker" : req.remark, req.time_in_force, req.fill_outside_rth);
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
  connected_ = false;
}

}  // namespace opend
}  // namespace mmtrade
