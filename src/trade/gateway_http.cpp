#include "trade/gateway_http.hpp"

#include "trade/order_errors.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

namespace mmtrade {
namespace trade {

namespace {

std::string LowerCopy(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

}  // namespace

PlaceOrderResult PlaceResultFromGatewayHttpError(int http_code, const std::string &body) {
  try {
    const nlohmann::json data = nlohmann::json::parse(body);
    if (data.is_object() &&
        (data.contains("ok") || data.contains("ret_code") || data.contains("error"))) {
      return PlaceOrderResult(data.value("ok", false), data.value("ret_code", -1),
                              data.value("order_id", ""), data.value("status", ""),
                              data.value("error", ""));
    }
  } catch (...) {
  }
  return MakeBrokerFailure(-1, std::string("gateway_http_error ") + std::to_string(http_code) +
                                   ": " + body);
}

std::string GatewayErrorText(const PlaceOrderResult &res) { return res.error; }

bool IsGatewayInflightTimeout(const PlaceOrderResult &res) {
  const std::string err = LowerCopy(res.error);
  return err.find("gateway timeout waiting result") != std::string::npos ||
         err.find("gateway_http_timeout") != std::string::npos ||
         err.find("timed out") != std::string::npos;
}

bool IsTransientGatewayInfraFailure(const PlaceOrderResult &res) {
  if (res.ok || res.ret_code != -1) {
    return false;
  }
  if (IsGatewayInflightTimeout(res)) {
    return false;
  }
  const std::string err = LowerCopy(res.error);
  return err.find("gateway_unreachable") != std::string::npos ||
         err.find("gateway_bad_response") != std::string::npos;
}

bool ShouldFallbackToDirect(const PlaceOrderResult &res) {
  if (res.ok) {
    return false;
  }
  if (res.ret_code != -1) {
    return false;
  }
  if (IsGatewayInflightTimeout(res)) {
    return false;
  }
  const std::string err = LowerCopy(res.error);
  const bool infra = err.find("gateway_unreachable") != std::string::npos ||
                     err.find("gateway_http_error") != std::string::npos ||
                     err.find("gateway execute exception") != std::string::npos ||
                     err.find("gateway_bad_response") != std::string::npos ||
                     err.find("missing result") != std::string::npos;
  if (!infra) {
    return false;
  }
  if (IsOrderRateLimitErr(GatewayErrorText(res))) {
    return false;
  }
  return true;
}

}  // namespace trade
}  // namespace mmtrade
