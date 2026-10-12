#include "gateway/http_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

namespace mmtrade {
namespace gateway {

namespace {

bool SetReuseAddr(int fd) {
  int yes = 1;
  return ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == 0;
}

bool RecvAllHeaders(int fd, std::string *out) {
  out->clear();
  char buf[1024];
  while (out->find("\r\n\r\n") == std::string::npos) {
    const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
    if (n <= 0) {
      return false;
    }
    out->append(buf, static_cast<std::size_t>(n));
    if (out->size() > 1024 * 1024) {
      return false;
    }
  }
  return true;
}

std::size_t ContentLengthOf(const std::string &headers) {
  const std::string key = "Content-Length:";
  std::size_t pos = headers.find(key);
  if (pos == std::string::npos) {
    pos = headers.find("content-length:");
  }
  if (pos == std::string::npos) {
    return 0;
  }
  pos += key.size();
  while (pos < headers.size() && (headers[pos] == ' ' || headers[pos] == '\t')) {
    ++pos;
  }
  return static_cast<std::size_t>(std::atoi(headers.c_str() + pos));
}

bool RecvExact(int fd, std::size_t need, std::string *out) {
  out->clear();
  out->reserve(need);
  char buf[4096];
  while (out->size() < need) {
    const std::size_t remain = need - out->size();
    const ssize_t n =
        ::recv(fd, buf, remain > sizeof(buf) ? sizeof(buf) : remain, 0);
    if (n <= 0) {
      return false;
    }
    out->append(buf, static_cast<std::size_t>(n));
  }
  return true;
}

void SendAll(int fd, const std::string &data) {
  std::size_t sent = 0;
  while (sent < data.size()) {
    const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, 0);
    if (n <= 0) {
      return;
    }
    sent += static_cast<std::size_t>(n);
  }
}

void SendJson(int fd, int status, const nlohmann::json &payload) {
  const std::string body = payload.dump();
  std::ostringstream oss;
  oss << "HTTP/1.1 " << status << " OK\r\n"
      << "Content-Type: application/json; charset=utf-8\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Connection: close\r\n\r\n"
      << body;
  SendAll(fd, oss.str());
}

std::string PathOnly(const std::string &target) {
  const std::size_t q = target.find('?');
  return q == std::string::npos ? target : target.substr(0, q);
}

}  // namespace

GatewayHttpServer::GatewayHttpServer(GatewayRuntime *runtime)
    : runtime_(runtime), listen_fd_(-1), running_(false) {}

GatewayHttpServer::~GatewayHttpServer() { Stop(); }

bool GatewayHttpServer::Start() {
  if (runtime_ == NULL) {
    return false;
  }
  listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) {
    return false;
  }
  SetReuseAddr(listen_fd_);
  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(runtime_->config().bind_port));
  if (::inet_pton(AF_INET, runtime_->config().bind_host.c_str(), &addr.sin_addr) != 1) {
    ::close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }
  if (::bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    ::close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }
  if (::listen(listen_fd_, 128) != 0) {
    ::close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }
  running_ = true;
  accept_thread_ = std::thread(&GatewayHttpServer::AcceptLoop, this);
  return true;
}

void GatewayHttpServer::ServeForever() {
  while (running_.load()) {
    ::sleep(1);
  }
}

void GatewayHttpServer::Stop() {
  if (!running_.exchange(false)) {
    if (listen_fd_ >= 0) {
      ::close(listen_fd_);
      listen_fd_ = -1;
    }
    return;
  }
  if (listen_fd_ >= 0) {
    ::shutdown(listen_fd_, SHUT_RDWR);
    ::close(listen_fd_);
    listen_fd_ = -1;
  }
  if (accept_thread_.joinable()) {
    accept_thread_.join();
  }
}

void GatewayHttpServer::AcceptLoop() {
  while (running_.load()) {
    const int fd = ::accept(listen_fd_, NULL, NULL);
    if (fd < 0) {
      if (!running_.load()) {
        break;
      }
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    std::thread(&GatewayHttpServer::HandleClient, this, fd).detach();
  }
}

void GatewayHttpServer::HandleClient(int fd) {
  std::string head;
  if (!RecvAllHeaders(fd, &head)) {
    ::close(fd);
    return;
  }
  const std::size_t line_end = head.find("\r\n");
  if (line_end == std::string::npos) {
    ::close(fd);
    return;
  }
  const std::string req_line = head.substr(0, line_end);
  std::istringstream iss(req_line);
  std::string method;
  std::string target;
  std::string version;
  iss >> method >> target >> version;
  const std::string path = PathOnly(target);
  const std::size_t hdr_end = head.find("\r\n\r\n");
  const std::string headers = head.substr(0, hdr_end);
  std::string body_prefix = head.substr(hdr_end + 4);
  const std::size_t content_len = ContentLengthOf(headers);
  std::string body = body_prefix;
  if (body.size() < content_len) {
    std::string more;
    if (!RecvExact(fd, content_len - body.size(), &more)) {
      ::close(fd);
      return;
    }
    body += more;
  } else if (body.size() > content_len) {
    body.resize(content_len);
  }

  if (runtime_ == NULL) {
    SendJson(fd, 500, {{"ok", false}, {"error", "runtime not ready"}});
    ::close(fd);
    return;
  }

  if (method == "GET" && path == "/health") {
    SendJson(fd, 200, {{"ok", true}, {"service", "opend_gateway_cpp"}});
  } else if (method == "GET" && path == "/metrics") {
    SendJson(fd, 200, {{"ok", true}, {"metrics", runtime_->Metrics()}});
  } else if (method == "POST" && path == "/v1/orders/place") {
    nlohmann::json payload;
    try {
      payload = nlohmann::json::parse(body);
    } catch (...) {
      SendJson(fd, 400, {{"ok", false}, {"error", "invalid json"}});
      ::close(fd);
      return;
    }
    static const char *kRequired[] = {"request_id", "code", "side", "qty", "price"};
    for (std::size_t i = 0; i < sizeof(kRequired) / sizeof(kRequired[0]); ++i) {
      if (!payload.contains(kRequired[i])) {
        SendJson(fd, 400,
                 {{"ok", false},
                  {"error", std::string("missing field: ") + kRequired[i]}});
        ::close(fd);
        return;
      }
    }
    const nlohmann::json result = runtime_->Submit(payload);
    const int status = result.value("ok", false) ? 200 : 400;
    SendJson(fd, status, result);
  } else {
    SendJson(fd, 404, {{"ok", false}, {"error", "not found"}});
  }
  ::close(fd);
}

}  // namespace gateway
}  // namespace mmtrade
