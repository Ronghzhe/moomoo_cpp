#pragma once

#include "opend/account_types.hpp"
#include "opend/order_config.hpp"
#include "opend/trade_connection.hpp"
#include "trade/i_order_broker.hpp"
#include "trade/order_parse.hpp"

#include <memory>
#include <string>

namespace mmtrade {
namespace opend {

// High-level client mirroring Python order_client.OrderClient (SIMULATE skeleton).
class OrderClient {
 public:
  explicit OrderClient(const OrderConfig &config, TradeConnection *shared_conn = NULL);
  ~OrderClient();

  bool Connect();
  void Close();
  bool trade_connected() const;

  // Non-owning. When set, Place() routes through broker (gateway / fallback).
  // Cancel/query/push still use the local OpenD connection.
  void SetPlaceBroker(trade::IOrderBroker *broker);
  trade::IOrderBroker *PlaceBroker() const { return place_broker_; }

  trade::OrderPushCache &EnsurePushHub();
  bool EnsurePushSubscription();
  const trade::OrderPushCache *PushHub() const;

  PlaceOrderResult Place(const std::string &code_full, const std::string &side, double qty,
                         double price, const std::string &order_type = "NORMAL",
                         const std::string &remark = "", const std::string &time_in_force = "DAY",
                         bool fill_outside_rth = false,
                         const std::string &sdk_session = "");

  bool CancelOrder(std::uint64_t order_id, const std::string &code_full,
                   const std::string &order_id_ex = "");
  bool QueryOrder(std::uint64_t order_id, const std::string &code_full,
                  const std::string &order_id_ex = "");
  bool GetPositions(const std::string &code_full, PositionList *out);

  const OrderConfig &config() const { return config_; }
  TradeConnection *TradeConn() const { return conn_; }

 private:
  OrderConfig config_;
  TradeConnection *conn_;
  std::unique_ptr<TradeConnection> owned_conn_;
  bool push_ready_;
  trade::IOrderBroker *place_broker_;
};

}  // namespace opend
}  // namespace mmtrade
