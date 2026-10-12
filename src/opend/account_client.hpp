#pragma once

#include "common/result.hpp"
#include "opend/account_config.hpp"
#include "opend/account_types.hpp"

#include <memory>
#include <string>

namespace mmtrade {
namespace opend {

class TradeConnection;

// Mirrors Python account_client.AccountClient (SIMULATE skeleton).
class AccountClient {
 public:
  explicit AccountClient(const AccountConfig &config);
  ~AccountClient();

  bool Connect();
  void Close();

  common::Result<AccountList> GetAccountList();
  common::Result<FundsInfo> GetFunds();
  common::Result<PositionList> GetPositions(const std::string &code = "");

  TradeConnection *Raw();
  const AccountConfig &config() const { return config_; }

 private:
  AccountConfig config_;
  std::unique_ptr<TradeConnection> conn_;
};

}  // namespace opend
}  // namespace mmtrade
