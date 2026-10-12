#pragma once

#include "FTAPIDemoBase.h"
#include "quote/kline_parse.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace mmtrade {
namespace opend {

struct KlineBar {
  std::string time_key;
  std::int64_t timestamp_s;
  double open;
  double high;
  double low;
  double close;
  double volume;
  double turnover;
};

struct KlineFetchResult {
  bool ok;
  int ret_code;
  std::string error;
  std::vector<KlineBar> bars;

  KlineFetchResult() : ok(false), ret_code(-1) {}
};

class KlineConnection : public FTAPIDemoBase {
 public:
  KlineConnection();

  bool Connect(const std::string &host, int port);
  void Disconnect();

  KlineFetchResult Fetch(const std::string &code_full, quote::KlTypeKey kl_type, int req_num,
                         bool extended_time = true, bool skip_subscribe = false);

 private:
  Qot_Common::QotMarket ToSdkMarket(const quote::QuoteSymbol &sym) const;
  Qot_Common::KLType ToSdkKlType(quote::KlTypeKey key) const;
  Qot_Common::SubType ToSdkSubType(quote::KlTypeKey key) const;
  Qot_Common::RehabType ToSdkRehab(quote::AuTypeKey key) const;

  void OnReply_Sub(Moomoo::u32_t, const Qot_Sub::Response &stRsp) override;
  void OnReply_GetKL(Moomoo::u32_t, const Qot_GetKL::Response &stRsp) override;

  bool connected_;
  bool get_ok_;
  std::vector<KlineBar> last_bars_;
  std::string last_error_;
};

}  // namespace opend
}  // namespace mmtrade
