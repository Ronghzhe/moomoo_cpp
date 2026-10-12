#pragma once

#include <string>

namespace mmtrade {
namespace log {

std::string LedgerNowIso(const std::string &symbol);

std::string RuntimeUpdatedAtUtc();

std::string FormatDatetimeBeijing();

}  // namespace log
}  // namespace mmtrade
