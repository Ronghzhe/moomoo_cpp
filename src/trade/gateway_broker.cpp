#include "trade/gateway_broker.hpp"

#include "trade/gateway_http.hpp"
#include "trade/order_id_util.hpp"

#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>
#include <sstream>

namespace mmtrade {
namespace trade {

namespace {

struct ParsedUrl {
  std::string host;
  int port;
  std::string path;
};

bool ParseHttpUrl(const std::string &url, ParsedUrl *out) {
  if (out == NULL) {
    return false;
  }
  std::string u = url;
  if (u.compare(0, 7, "http://") == 0) {
    u = u.substr(7);
  } else if (u.compare(0, 8, "https://") == 0) {
    return false;
  }
  const std::size_t slash = u.find('/');
  const std::string hostport = slash == std::string::npos ? u : u.substr(0, slash);
  out->path = slash == std::string::npos ? "/" : u.substr(slash);
  const std::size_t colon = hostport.find(':');
  if (colon == std::string::npos) {
    out->host = hostport;
    out->port = 80;
  } else {
    out->host = hostport.substr(0, colon);
    out->port = std::atoi(hostport.c_str() + colon + 1);
  }
  return !out->host.empty() && out->port > 0;
}

bool RecvUntil(int fd, const std::string &marker, std::string *out, std::size_t max_bytes) {
  out->clear();
  char buf[2048];
  while (out->find(marker) == std::string::npos) {
    const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
    if (n <= 0) {
      return false;
    }
    out->append(buf, static_cast<std::size_t>(n));
    if (out->size() > max_bytes) {
      return false;
    }
  }
  return true;
}

std::size_t HeaderContentLength(const std::string &headers) {
  const char *keys[] = {"Content-Length:", "content-length:"};
  for (std::size_t i = 0; i < 2; ++i) {
    const std::size_t pos = headers.find(keys[i]);
    if (pos == std::string::npos) {
      continue;
    }
    std::size_t p = pos + std::strlen(keys[i]);
    while (p < headers.size() && (headers[p] == ' ' || headers[p] == '\t')) {
      ++p;
    }
    return static_cast<std::size_t>(std::atoi(headers.c_str() + p));
  }
  return 0;
}

PlaceOrderResult HttpPostJson(const std::string &base_url, const std::string &path,
                              const nlohmann::json &payload, double timeout_sec) {
  ParsedUrl url;
  if (!ParseHttpUrl(base_url, &url)) {
    return MakeBrokerFailure(-1, "gateway_unreachable: bad url");
  }
  const std::string full_path = path.empty() ? url.path : path;

  using clock = std::chrono::steady_clock;
  const clock::time_point t0 = clock::now();

  addrinfo hints;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo *res = NULL;
  const std::string port_str = std::to_string(url.port);
  if (::getaddrinfo(url.host.c_str(), port_str.c_str(), &hints, &res) != 0 || res == NULL) {
    return MakeBrokerFailure(-1, "gateway_unreachable: resolve failed");
  }

  const int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd < 0) {
    ::freeaddrinfo(res);
    return MakeBrokerFailure(-1, "gateway_unreachable: socket failed");
  }

  timeval tv;
  tv.tv_sec = static_cast<time_t>(timeout_sec > 0 ? timeout_sec : 8.0);
  tv.tv_usec = 0;
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  if (::connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
    ::freeaddrinfo(res);
    ::close(fd);
    return MakeBrokerFailure(-1, "gateway_unreachable: connect failed");
  }
  ::freeaddrinfo(res);

  const std::string body = payload.dump();
  std::ostringstream req;
  req << "POST " << full_path << " HTTP/1.1\r\n"
      << "Host: " << url.host << "\r\n"
      << "Content-Type: application/json; charset=utf-8\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Connection: close\r\n\r\n"
      << body;
  const std::string raw_req = req.str();
  std::size_t sent = 0;
  while (sent < raw_req.size()) {
    const ssize_t n = ::send(fd, raw_req.data() + sent, raw_req.size() - sent, 0);
    if (n <= 0) {
      ::close(fd);
      return MakeBrokerFailure(-1, "gateway_unreachable: send failed");
    }
    sent += static_cast<std::size_t>(n);
  }

  std::string head;
  if (!RecvUntil(fd, "\r\n\r\n", &head, 1024 * 1024)) {
    ::close(fd);
    return MakeBrokerFailure(-1, "gateway_http_timeout: recv headers");
  }
  const std::size_t hdr_end = head.find("\r\n\r\n");
  const std::string headers = head.substr(0, hdr_end);
  std::string resp_body = head.substr(hdr_end + 4);
  const std::size_t content_len = HeaderContentLength(headers);
  while (resp_body.size() < content_len) {
    char buf[4096];
    const std::size_t need = content_len - resp_body.size();
    const ssize_t n = ::recv(fd, buf, need > sizeof(buf) ? sizeof(buf) : need, 0);
    if (n <= 0) {
      ::close(fd);
      return MakeBrokerFailure(-1, "gateway_http_timeout: recv body");
    }
    resp_body.append(buf, static_cast<std::size_t>(n));
  }
  ::close(fd);
  const std::int64_t http_rtt_us =
      std::chrono::duration_cast<std::chrono::microseconds>(clock::now() - t0).count();

  int http_code = 0;
  {
    const std::size_t line_end = headers.find("\r\n");
    const std::string status_line =
        line_end == std::string::npos ? headers : headers.substr(0, line_end);
    std::istringstream iss(status_line);
    std::string http_ver;
    iss >> http_ver >> http_code;
  }

  auto fill_timings = [&](PlaceOrderResult *out, const nlohmann::json &data) {
    if (out == NULL) {
      return;
    }
    out->http_rtt_us = http_rtt_us;
    out->place_via = "gateway";
    if (data.contains("queue_wait_ms") && data["queue_wait_ms"].is_number()) {
      out->queue_wait_us =
          static_cast<std::int64_t>(data["queue_wait_ms"].get<double>() * 1000.0);
    }
    if (data.contains("rate_wait_ms") && data["rate_wait_ms"].is_number()) {
      out->rate_wait_us =
          static_cast<std::int64_t>(data["rate_wait_ms"].get<double>() * 1000.0);
    }
    if (data.contains("place_pre_us") && data["place_pre_us"].is_number_integer()) {
      out->place_pre_us = data["place_pre_us"].get<std::int64_t>();
    }
    if (data.contains("place_sdk_us") && data["place_sdk_us"].is_number_integer()) {
      out->place_sdk_us = data["place_sdk_us"].get<std::int64_t>();
    }
    if (data.contains("place_post_us") && data["place_post_us"].is_number_integer()) {
      out->place_post_us = data["place_post_us"].get<std::int64_t>();
    }
  };

  if (http_code >= 400) {
    PlaceOrderResult out = PlaceResultFromGatewayHttpError(http_code, resp_body);
    try {
      fill_timings(&out, nlohmann::json::parse(resp_body));
    } catch (...) {
      out.http_rtt_us = http_rtt_us;
      out.place_via = "gateway";
    }
    return out;
  }

  try {
    const nlohmann::json data = nlohmann::json::parse(resp_body);
    PlaceOrderResult out(data.value("ok", false), data.value("ret_code", -1),
                         data.value("order_id", ""), data.value("status", ""),
                         data.value("error", ""));
    out.internal_order_id = ParseOrderIdUint64(out.order_id);
    fill_timings(&out, data);
    return out;
  } catch (...) {
    PlaceOrderResult out = MakeBrokerFailure(-1, std::string("gateway_bad_response: ") + resp_body);
    out.http_rtt_us = http_rtt_us;
    out.place_via = "gateway";
    return out;
  }
}

}  // namespace

GatewayBroker::GatewayBroker(const GatewayBrokerConfig &cfg) : cfg_(cfg) {
  if (!cfg_.base_url.empty() && cfg_.base_url[cfg_.base_url.size() - 1] == '/') {
    cfg_.base_url.erase(cfg_.base_url.size() - 1);
  }
}

GatewayBroker::~GatewayBroker() { Close(); }

void GatewayBroker::Close() {}

PlaceOrderResult GatewayBroker::PlaceOrder(const PlaceOrderRequest &req) {
  std::string rid = req.request_id;
  if (rid.empty()) {
    rid = std::string("cpp-") + std::to_string(
                                    static_cast<long long>(::time(NULL) * 1000LL));
  }
  nlohmann::json payload = {
      {"request_id", rid},
      {"code", req.code},
      {"side", req.side},
      {"qty", req.qty},
      {"price", req.price},
      {"order_type", req.order_type},
      {"remark", req.remark},
      {"fill_outside_rth", req.fill_outside_rth},
      {"time_in_force", req.time_in_force},
      {"session", req.session},
      {"context",
       {{"strategy_id", req.context.strategy_id},
        {"symbol", req.context.symbol},
        {"account_key", req.context.account_key},
        {"acc_id", req.context.acc_id},
        {"acc_index", req.context.acc_index},
        {"trd_env", req.context.trd_env},
        {"trd_market", req.context.trd_market},
        {"opend_host", req.context.opend_host},
        {"opend_port", req.context.opend_port},
        {"priority", req.context.priority}}}};
  return HttpPostJson(cfg_.base_url, "/v1/orders/place", payload, cfg_.timeout_sec);
}

}  // namespace trade
}  // namespace mmtrade
