#include "l1_common.hpp"
#include "opend/order_client.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

int main(int argc, char **argv) {
  l1::CommonArgs args;
  l1::ParseCommonArgs(argc, argv, &args);
  double qty = 1.0;
  double price = 0.01;
  std::uint64_t account_id = 0;
  std::string side = "BUY";
  std::string order_type = "NORMAL";

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--qty" && i + 1 < argc) {
      qty = std::atof(argv[++i]);
    } else if (a == "--price" && i + 1 < argc) {
      price = std::atof(argv[++i]);
    } else if (a == "--acc-id" && i + 1 < argc) {
      account_id = static_cast<std::uint64_t>(std::strtoull(argv[++i], NULL, 10));
    } else if (a == "--side" && i + 1 < argc) {
      side = argv[++i];
    } else if (a == "--order-type" && i + 1 < argc) {
      order_type = argv[++i];
    }
  }

  l1::SdkInit();

  mmtrade::opend::OrderConfig cfg;
  cfg.host = args.host;
  cfg.port = args.port;
  cfg.simulate = true;
  cfg.acc_id = account_id;

  mmtrade::opend::OrderClient client(cfg);
  bool ok_connect = client.Connect();
  bool ok_place = false;
  bool ok_query = false;
  bool ok_cancel = false;
  bool ok_push = false;
  std::uint64_t order_id = 0;
  std::string err;
  mmtrade::trade::OrderPushRow latest_row;
  mmtrade::opend::PlaceOrderResult pr;

  if (!ok_connect) {
    err = "connect failed";
  } else {
    mmtrade::trade::OrderPushCache &hub = client.EnsurePushHub();
    pr = client.Place(args.code, side, qty, price, order_type, "l1_contract");
    ok_place = pr.ok;
    order_id = pr.order_id;
    err = pr.error;
    if (ok_place) {
      const std::string display_id = pr.order_id_ex.empty()
          ? std::to_string(order_id)
          : pr.order_id_ex;
      const std::uint64_t ver0 = hub.GetVersion(display_id);
      ok_push = hub.WaitUpdate(display_id, ver0, 5.0, &latest_row);
      ok_query = client.QueryOrder(order_id, args.code);
      hub.GetRow(display_id, &latest_row);
      ok_cancel = client.CancelOrder(order_id, args.code);
    }
  }

  client.Close();

  const bool ok_all = ok_connect && ok_place && ok_query && ok_cancel;
  std::ostringstream out;
  out << "{\"case\":\"l1_contract\",\"lang\":\"cpp\",\"trd_env\":\"SIMULATE\""
      << ",\"ok\":" << (ok_all ? "true" : "false")
      << ",\"ok_connect\":" << (ok_connect ? "true" : "false")
      << ",\"ok_place\":" << (ok_place ? "true" : "false")
      << ",\"ok_push\":" << (ok_push ? "true" : "false")
      << ",\"ok_query\":" << (ok_query ? "true" : "false")
      << ",\"ok_cancel\":" << (ok_cancel ? "true" : "false")
      << ",\"order_id\":" << order_id << ",\"code\":\"" << l1::JsonEscape(args.code)
      << "\",\"account_id\":" << account_id
      << ",\"ret_code\":" << pr.ret_code
      << ",\"response_source\":\"" << l1::JsonEscape(pr.response_source)
      << "\",\"raw_response\":\"" << l1::JsonEscape(pr.raw_response) << "\"";
  if (ok_place) {
    out << ",\"order_status\":\"" << l1::JsonEscape(latest_row.order_status)
        << "\",\"dealt_qty\":" << latest_row.dealt_qty;
  }
  if (!err.empty()) {
    out << ",\"error\":\"" << l1::JsonEscape(err) << "\"";
  }
  out << "}";

  std::cout << out.str() << std::endl;

  l1::SdkShutdown();
  return ok_all ? 0 : 1;
}
