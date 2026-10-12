#pragma once

#include "common/result.hpp"
#include "opend/kline_connection.hpp"
#include "quote/kline_config.hpp"
#include "quote/kline_parse.hpp"

#include <memory>
#include <string>

namespace mmtrade {
namespace opend {

typedef quote::KlineConfig KlineConfig;

// Mirrors Python kline_client.KlineClient (fetch path only).
class KlineService {
 public:
  explicit KlineService(const KlineConfig &config);
  ~KlineService();

  bool Connect();
  void Close();

  common::Result<KlineFetchResult> FetchLatest();

  const KlineConfig &config() const { return config_; }

 private:
  KlineConfig config_;
  std::unique_ptr<KlineConnection> conn_;
};

}  // namespace opend
}  // namespace mmtrade
