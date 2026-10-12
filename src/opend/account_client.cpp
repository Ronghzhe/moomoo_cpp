#include "opend/account_client.hpp"

#include "opend/symbol_util.hpp"
#include "opend/trade_connection.hpp"

namespace mmtrade {
namespace opend {

AccountClient::AccountClient(const AccountConfig &config) : config_(config) {
  conn_.reset(new TradeConnection());
}

AccountClient::~AccountClient() { Close(); }

bool AccountClient::Connect() {
  if (conn_.get() == NULL) {
    conn_.reset(new TradeConnection());
  }
  conn_->set_simulate(config_.simulate);
  if (config_.acc_id != 0) {
    conn_->set_prefer_acc_id(config_.acc_id);
  }
  return conn_->Connect(config_.host, config_.port, "");
}

void AccountClient::Close() {
  if (conn_.get() != NULL) {
    conn_->Disconnect();
  }
}

TradeConnection *AccountClient::Raw() { return conn_.get(); }

common::Result<AccountList> AccountClient::GetAccountList() {
  if (conn_.get() == NULL) {
    return common::Result<AccountList>::Fail(-1, "not connected");
  }
  return common::Result<AccountList>::Ok(conn_->GetAccountListCached());
}

common::Result<FundsInfo> AccountClient::GetFunds() {
  if (conn_.get() == NULL) {
    return common::Result<FundsInfo>::Fail(-1, "not connected");
  }
  std::string probe = "US.AAPL";
  if (config_.trd_market == trade::TrdMarket::kHK) {
    probe = "HK.00700";
  }
  FundsInfo funds;
  if (!conn_->GetFunds(probe, &funds)) {
    return common::Result<FundsInfo>::Fail(-1, "GetFunds failed");
  }
  return common::Result<FundsInfo>::Ok(funds);
}

common::Result<PositionList> AccountClient::GetPositions(const std::string &code) {
  if (conn_.get() == NULL) {
    return common::Result<PositionList>::Fail(-1, "not connected");
  }
  std::string probe = "US.AAPL";
  if (config_.trd_market == trade::TrdMarket::kHK) {
    probe = "HK.00700";
  }
  PositionList rows;
  if (!conn_->GetPositions(probe, code, &rows)) {
    return common::Result<PositionList>::Fail(-1, "GetPositions failed");
  }
  return common::Result<PositionList>::Ok(rows);
}

}  // namespace opend
}  // namespace mmtrade
