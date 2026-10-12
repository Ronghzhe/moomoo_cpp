#include "notify/telegram_client.hpp"

#include <nlohmann/json.hpp>

#include <unistd.h>

#include <array>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace mmtrade {
namespace notify {

namespace {

bool IsValidUtf8(const std::string &s) {
  size_t i = 0;
  while (i < s.size()) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c <= 0x7F) {
      ++i;
      continue;
    }
    size_t need = 0;
    if ((c & 0xE0) == 0xC0) {
      need = 1;
    } else if ((c & 0xF0) == 0xE0) {
      need = 2;
    } else if ((c & 0xF8) == 0xF0) {
      need = 3;
    } else {
      return false;
    }
    if (i + need >= s.size()) {
      return false;
    }
    for (size_t j = 1; j <= need; ++j) {
      const unsigned char cc = static_cast<unsigned char>(s[i + j]);
      if ((cc & 0xC0) != 0x80) {
        return false;
      }
    }
    i += need + 1;
  }
  return true;
}

}  // namespace

std::string BuildTelegramSendMessagePayload(const std::string &chat_id, const std::string &text) {
  nlohmann::json payload;
  payload["chat_id"] = chat_id;
  payload["text"] = text;
  return payload.dump();
}

TelegramSendResult SendTelegramText(const std::string &bot_token, const std::string &chat_id,
                                    const std::string &text) {
  TelegramSendResult out;
  if (bot_token.empty() || chat_id.empty()) {
    out.error = "missing_token_or_chat_id";
    return out;
  }
  if (!IsValidUtf8(text)) {
    out.error = "text_not_valid_utf8";
    return out;
  }
  if (!IsValidUtf8(chat_id)) {
    out.error = "chat_id_not_valid_utf8";
    return out;
  }

  const std::string payload = BuildTelegramSendMessagePayload(chat_id, text);
  const std::string url = "https://api.telegram.org/bot" + bot_token + "/sendMessage";

  char tmpl[] = "/tmp/tg_payload_cpp_XXXXXX";
  const int fd = mkstemp(tmpl);
  if (fd < 0) {
    out.error = "mkstemp_failed";
    return out;
  }
  {
    if (write(fd, payload.data(), payload.size()) != static_cast<ssize_t>(payload.size())) {
      close(fd);
      unlink(tmpl);
      out.error = "write_payload_failed";
      return out;
    }
  }
  close(fd);

  std::ostringstream cmd;
  cmd << "curl -sS -m 20 -X POST " << url
      << " -H 'Content-Type: application/json; charset=utf-8' --data-binary @" << tmpl
      << " 2>&1";
  std::array<char, 512> buf;
  std::string response;
  FILE *pipe = popen(cmd.str().c_str(), "r");
  if (pipe == NULL) {
    unlink(tmpl);
    out.error = "popen_failed";
    return out;
  }
  while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != NULL) {
    response += buf.data();
  }
  const int rc = pclose(pipe);
  unlink(tmpl);

  if (rc != 0) {
    out.error = "curl_failed: " + response;
    return out;
  }
  if (response.find("\"ok\":true") != std::string::npos) {
    out.ok = true;
    return out;
  }
  out.error = response.empty() ? "telegram_not_ok" : response.substr(0, 400);
  return out;
}

}  // namespace notify
}  // namespace mmtrade
