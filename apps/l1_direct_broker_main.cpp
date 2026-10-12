#include "l1_common.hpp"
#include "opend/direct_broker.hpp"
#include "opend/order_client.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sstream>

namespace {

struct BrokerArgs {
  l1::CommonArgs common;
  double qty;
  double price;
};

bool ParseBrokerArgs(int argc, char **argv, BrokerArgs *out) {
  if (out == NULL) {
    return false;
  }
  out->qty = 1.0;
  out->price = 0.01;
  l1::ParseCommonArgs(argc, argv, &out->common);
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--qty" && i + 1 < argc) {
      out->qty = std::atof(argv[++i]);
    } else if (a == "--price" && i + 1 < argc) {
      out->price = std::atof(argv[++i]);
    }
  }
  return true;
}

}  // namespace

int main(int argc, char **argv) {
  BrokerArgs args;
  ParseBrokerArgs(argc, argv, &args);

  l1::SdkInit();

  mmtrade::trade::DirectBrokerConfig cfg;
  cfg.host = args.common.host;
  cfg.port = args.common.port;
  cfg.simulate = true;
  if (args.common.code.find("HK.") == 0) {
    cfg.trd_market = mmtrade::trade::TrdMarket::kHK;
  }

  mmtrade::opend::DirectBroker broker(cfg);
  mmtrade::trade::PlaceOrderRequest req;
  req.code = args.common.code;
  req.side = "BUY";
  req.qty = args.qty;
  req.price = args.price;
  req.order_type = "NORMAL";
  req.remark = "l1_direct_broker";
  req.context.symbol = args.common.code;

  const mmtrade::trade::PlaceOrderResult placed = broker.PlaceOrder(req);
  bool ok_place = placed.ok && !placed.order_id.empty();
  bool ok_cancel = false;
  std::string err = placed.error;

  if (ok_place && broker.Client() != NULL) {
    const std::uint64_t order_id = placed.internal_order_id;
    if (order_id != 0) {
      ok_cancel = broker.Client()->CancelOrder(order_id, args.common.code);
      if (!ok_cancel && err.empty()) {
        err = "cancel failed";
      }
    } else if (err.empty()) {
      err = "missing internal order_id for cancel";
    }
  }

  broker.Close();

  const bool ok_all = ok_place && ok_cancel;
  std::ostringstream out;
  out << "{\"case\":\"l1_direct_broker\",\"lang\":\"cpp\",\"trd_env\":\"SIMULATE\""
      << ",\"ok\":" << (ok_all ? "true" : "false")
      << ",\"ok_place\":" << (ok_place ? "true" : "false")
      << ",\"ok_cancel\":" << (ok_cancel ? "true" : "false")
      << ",\"order_id\":\"" << l1::JsonEscape(placed.order_id) << "\",\"code\":\""
      << l1::JsonEscape(args.common.code) << "\"";
  if (!err.empty()) {
    out << ",\"error\":\"" << l1::JsonEscape(err) << "\"";
  }
  out << "}";

  std::cout << out.str() << std::endl;

  l1::SdkShutdown();
  return ok_all ? 0 : 1;
}
