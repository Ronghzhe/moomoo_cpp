#include "opend/broker_factory.hpp"

#include <cstdlib>

namespace mmtrade {
namespace opend {

namespace {

std::string LowerEnv(const char *name, const char *fallback) {
  const char *raw = std::getenv(name);
  std::string s = (raw == NULL || raw[0] == '\0') ? std::string(fallback) : std::string(raw);
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] >= 'A' && s[i] <= 'Z') {
      s[i] = static_cast<char>(s[i] - 'A' + 'a');
    }
  }
  return s;
}

}  // namespace

std::string OrderBrokerModeFromEnv() {
  return LowerEnv("ORDER_BROKER_MODE", "");
}

double GatewayHttpTimeoutSecFromEnv() {
  const char *raw = std::getenv("OPEND_GATEWAY_HTTP_TIMEOUT_SEC");
  if (raw == NULL || raw[0] == '\0') {
    return 18.0;
  }
  const double v = std::atof(raw);
  return v < 5.0 ? 5.0 : v;
}

std::string GatewayUrlFromEnv() {
  const char *raw = std::getenv("OPEND_GATEWAY_URL");
  if (raw == NULL || raw[0] == '\0') {
    return "http://127.0.0.1:18766";
  }
  return std::string(raw);
}

std::unique_ptr<trade::IOrderBroker> CreateOrderBroker(const trade::DirectBrokerConfig &direct_cfg,
                                                        TradeConnection *shared_trade) {
  const std::string mode = OrderBrokerModeFromEnv();
  if (mode != "gateway" && mode != "gateway_fallback") {
    return std::unique_ptr<trade::IOrderBroker>();
  }
  trade::GatewayBrokerConfig gcfg;
  gcfg.base_url = GatewayUrlFromEnv();
  gcfg.timeout_sec = GatewayHttpTimeoutSecFromEnv();
  std::unique_ptr<trade::IOrderBroker> gateway(new trade::GatewayBroker(gcfg));
  std::unique_ptr<trade::IOrderBroker> direct(new DirectBroker(direct_cfg, shared_trade));
  return std::unique_ptr<trade::IOrderBroker>(
      new trade::FallbackBroker(std::move(gateway), std::move(direct)));
}

}  // namespace opend
}  // namespace mmtrade
