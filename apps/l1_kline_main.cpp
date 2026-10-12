#include "l1_common.hpp"
#include "opend/kline_service.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>

namespace {

struct KlineArgs {
  l1::CommonArgs common;
  std::string ktype;
  int bars;
};

bool ParseKlineArgs(int argc, char **argv, KlineArgs *out) {
  if (out == NULL) {
    return false;
  }
  out->ktype = "K_5M";
  out->bars = 80;
  l1::ParseCommonArgs(argc, argv, &out->common);
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--ktype" && i + 1 < argc) {
      out->ktype = argv[++i];
    } else if (a == "--bars" && i + 1 < argc) {
      out->bars = std::atoi(argv[++i]);
    }
  }
  return true;
}

}  // namespace

int main(int argc, char **argv) {
  KlineArgs args;
  ParseKlineArgs(argc, argv, &args);

  l1::SdkInit();

  mmtrade::quote::KlineConfig cfg;
  cfg.host = args.common.host;
  cfg.port = args.common.port;
  cfg.code = args.common.code;
  cfg.ktype = args.ktype;
  cfg.count = args.bars;

  mmtrade::opend::KlineService service(cfg);
  bool ok_connect = service.Connect();
  bool ok_fetch = false;
  int bar_count = 0;
  std::string err;

  if (!ok_connect) {
    err = "connect failed";
  } else {
    const mmtrade::common::Result<mmtrade::opend::KlineFetchResult> fetched = service.FetchLatest();
    ok_fetch = fetched.ok() && !fetched.value.bars.empty();
    if (ok_fetch) {
      bar_count = static_cast<int>(fetched.value.bars.size());
    } else if (fetched.ok()) {
      err = "empty kline bars";
    } else {
      err = fetched.error;
    }
  }

  service.Close();

  const bool ok_all = ok_connect && ok_fetch;
  std::ostringstream out;
  out << "{\"case\":\"l1_kline\",\"lang\":\"cpp\",\"trd_env\":\"SIMULATE\""
      << ",\"ok\":" << (ok_all ? "true" : "false")
      << ",\"ok_connect\":" << (ok_connect ? "true" : "false")
      << ",\"ok_fetch\":" << (ok_fetch ? "true" : "false")
      << ",\"symbol\":\"" << l1::JsonEscape(args.common.code) << "\",\"kl_type\":\""
      << l1::JsonEscape(args.ktype) << "\",\"bars_requested\":" << args.bars
      << ",\"bar_count\":" << bar_count;
  if (!err.empty()) {
    out << ",\"error\":\"" << l1::JsonEscape(err) << "\"";
  }
  out << "}";

  std::cout << out.str() << std::endl;

  l1::SdkShutdown();
  return ok_all ? 0 : 1;
}
