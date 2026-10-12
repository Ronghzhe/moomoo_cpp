#include "l1_common.hpp"
#include "opend/account_client.hpp"

#include <iostream>
#include <sstream>

int main(int argc, char **argv) {
  l1::CommonArgs args;
  l1::ParseCommonArgs(argc, argv, &args);
  std::uint64_t account_id = 0;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--acc-id" && i + 1 < argc) {
      account_id =
          static_cast<std::uint64_t>(std::strtoull(argv[++i], NULL, 10));
    }
  }

  l1::SdkInit();

  mmtrade::opend::AccountConfig cfg;
  cfg.host = args.host;
  cfg.port = args.port;
  cfg.simulate = true;
  cfg.acc_id = account_id;
  if (args.code.find("HK.") == 0) {
    cfg.trd_market = mmtrade::trade::TrdMarket::kHK;
  }

  mmtrade::opend::AccountClient client(cfg);
  bool ok_connect = client.Connect();
  bool ok_accounts = false;
  bool ok_funds = false;
  bool ok_positions = false;
  int account_count = 0;
  double cash = 0.0;
  int position_count = 0;
  std::string err;
  mmtrade::opend::AccountList accounts;
  mmtrade::opend::PositionList positions;

  if (!ok_connect) {
    err = "connect failed";
  } else {
    const mmtrade::common::Result<mmtrade::opend::AccountList> acc = client.GetAccountList();
    ok_accounts = acc.ok() && !acc.value.empty();
    account_count = ok_accounts ? static_cast<int>(acc.value.size()) : 0;
    if (!ok_accounts) {
      err = acc.ok() ? "empty account list" : acc.error;
    } else {
      accounts = acc.value;
      const mmtrade::common::Result<mmtrade::opend::FundsInfo> funds = client.GetFunds();
      ok_funds = funds.ok();
      if (ok_funds) {
        cash = funds.value.cash;
      } else if (err.empty()) {
        err = funds.error;
      }

      const mmtrade::common::Result<mmtrade::opend::PositionList> pos =
          client.GetPositions(args.code);
      ok_positions = pos.ok();
      if (ok_positions) {
        position_count = static_cast<int>(pos.value.size());
        positions = pos.value;
      } else if (err.empty()) {
        err = pos.error;
      }
    }
  }

  client.Close();

  const bool ok_all = ok_connect && ok_accounts && ok_funds && ok_positions;
  std::ostringstream out;
  out << "{\"case\":\"l1_account\",\"lang\":\"cpp\",\"trd_env\":\"SIMULATE\""
      << ",\"ok\":" << (ok_all ? "true" : "false")
      << ",\"ok_connect\":" << (ok_connect ? "true" : "false")
      << ",\"ok_accounts\":" << (ok_accounts ? "true" : "false")
      << ",\"ok_funds\":" << (ok_funds ? "true" : "false")
      << ",\"ok_positions\":" << (ok_positions ? "true" : "false")
      << ",\"account_count\":" << account_count << ",\"cash\":" << cash
      << ",\"position_count\":" << position_count << ",\"code\":\"" << l1::JsonEscape(args.code)
      << "\",\"account_id\":" << account_id;
  if (ok_accounts) {
    out << ",\"accounts\":[";
    for (std::size_t i = 0; i < accounts.size(); ++i) {
      if (i != 0) {
        out << ',';
      }
      out << "{\"acc_id\":" << accounts[i].acc_id
          << ",\"simulate\":" << (accounts[i].simulate ? "true" : "false") << "}";
    }
    out << ']';
  }
  if (ok_positions) {
    out << ",\"positions\":[";
    for (std::size_t i = 0; i < positions.size(); ++i) {
      if (i != 0) {
        out << ',';
      }
      out << "{\"code\":\"" << l1::JsonEscape(positions[i].code)
          << "\",\"qty\":" << positions[i].qty
          << ",\"can_sell_qty\":" << positions[i].can_sell_qty
          << ",\"position_side\":\""
          << l1::JsonEscape(positions[i].position_side) << "\"}";
    }
    out << ']';
  }
  if (!err.empty()) {
    out << ",\"error\":\"" << l1::JsonEscape(err) << "\"";
  }
  out << "}";

  std::cout << out.str() << std::endl;

  l1::SdkShutdown();
  return ok_all ? 0 : 1;
}
