#include "opend/kline_service.hpp"

namespace mmtrade {
namespace opend {

KlineService::KlineService(const KlineConfig &config) : config_(config) {
  conn_.reset(new KlineConnection());
}

KlineService::~KlineService() { Close(); }

bool KlineService::Connect() {
  if (conn_.get() == NULL) {
    conn_.reset(new KlineConnection());
  }
  return conn_->Connect(config_.host, config_.port);
}

void KlineService::Close() {
  if (conn_.get() != NULL) {
    conn_->Disconnect();
  }
}

common::Result<KlineFetchResult> KlineService::FetchLatest() {
  if (conn_.get() == NULL) {
    return common::Result<KlineFetchResult>::Fail(-1, "not connected");
  }
  const quote::KlTypeKey kl = quote::ParseKlTypeKey(config_.ktype);
  const KlineFetchResult fetched =
      conn_->Fetch(config_.code, kl, config_.count, true, config_.skip_subscribe);
  if (!fetched.ok) {
    return common::Result<KlineFetchResult>::Fail(fetched.ret_code, fetched.error);
  }
  return common::Result<KlineFetchResult>::Ok(fetched);
}

}  // namespace opend
}  // namespace mmtrade
