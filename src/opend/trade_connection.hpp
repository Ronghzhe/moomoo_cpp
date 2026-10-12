#pragma once

#include "FTAPIDemoBase.h"
#include "opend/account_types.hpp"
#include "opend/order_config.hpp"
#include "trade/order_push_cache.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace mmtrade {
namespace opend {

class TradeConnection : public FTAPIDemoBase {
 public:
  TradeConnection();

  bool Connect(const std::string &host, int port, const std::string &unlock_pwd_md5);
  void Disconnect();
  bool trade_connected() const { return connected_ && m_pTrdApi != nullptr && m_bTrdInitSuc; }

  void set_simulate(bool simulate) { simulate_ = simulate; }
  bool simulate() const { return simulate_; }

  bool EnsureAccPush(std::uint64_t prefer_acc_id);
  std::uint64_t ResolveAccId(const std::string &code_full);

  PlaceOrderResult Place(const PlaceOrderParams &params);
  bool CancelOrder(std::uint64_t order_id, const std::string &code_full);
  bool QueryOrder(std::uint64_t order_id, const std::string &code_full);

  AccountList GetAccountListCached() const;
  bool GetFunds(const std::string &code_full, FundsInfo *out);
  bool GetPositions(const std::string &code_full, const std::string &filter_code, PositionList *out);

  trade::OrderPushCache &PushCache() { return push_cache_; }
  const trade::OrderPushCache &PushCache() const { return push_cache_; }

  bool push_attached() const { return push_attached_; }

  void set_prefer_acc_id(std::uint64_t acc_id) { prefer_acc_id_ = acc_id; }

 protected:
  void OnReply_PlaceOrder(Moomoo::u32_t nSerialNo, const Trd_PlaceOrder::Response &stRsp) override;
  void OnReply_GetOrderList(Moomoo::u32_t nSerialNo, const Trd_GetOrderList::Response &stRsp) override;
  void OnReply_ModifyOrder(Moomoo::u32_t nSerialNo, const Trd_ModifyOrder::Response &stRsp) override;
  void OnReply_SubAccPush(Moomoo::u32_t, const Trd_SubAccPush::Response &stRsp) override;
  void OnReply_GetFunds(Moomoo::u32_t, const Trd_GetFunds::Response &stRsp) override;
  void OnReply_GetPositionList(Moomoo::u32_t, const Trd_GetPositionList::Response &stRsp) override;
  void OnPush_UpdateOrder(const Trd_UpdateOrder::Response &stRsp) override;
  void OnPush_UpdateOrderFill(const Trd_UpdateOrderFill::Response &stRsp) override;
  void OnDisConnect(MMAPI_Conn *pConn, Moomoo::i64_t nErrCode) override;

 private:
  Trd_Common::TrdSide ToSdkSide(trade::TrdSide side) const;
  Trd_Common::OrderType ToSdkOrderType(trade::OrderType ot) const;
  Trd_Common::TimeInForce ToSdkTif(trade::TimeInForce tif) const;
  void IngestOrderPush(std::uint64_t order_id, const std::string &order_id_ex,
                       const std::string &status, double dealt_qty, double dealt_avg_price,
                       const std::string &code = std::string(), std::int64_t create_ts_ms = 0,
                       std::int64_t fill_ts_ms = 0);
  void EnsureTrdApi();

  bool connected_;
  bool simulate_;
  bool push_attached_;
  bool sub_acc_push_ok_;
  bool place_ok_;
  bool query_ok_;
  bool modify_ok_;
  bool funds_ok_;
  bool positions_ok_;
  FundsInfo last_funds_;
  PositionList last_positions_;
  std::uint64_t last_order_id_;
  std::string last_order_id_ex_;
  std::string last_error_;
  std::string last_place_response_raw_;
  std::uint64_t prefer_acc_id_;
  trade::OrderPushCache push_cache_;
};

}  // namespace opend
}  // namespace mmtrade
