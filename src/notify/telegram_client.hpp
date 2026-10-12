#pragma once

#include <string>

namespace mmtrade {
namespace notify {

struct TelegramSendResult {
  bool ok;
  std::string error;

  TelegramSendResult() : ok(false) {}
};

std::string BuildTelegramSendMessagePayload(const std::string &chat_id, const std::string &text);

TelegramSendResult SendTelegramText(const std::string &bot_token, const std::string &chat_id,
                                    const std::string &text);

}  // namespace notify
}  // namespace mmtrade
