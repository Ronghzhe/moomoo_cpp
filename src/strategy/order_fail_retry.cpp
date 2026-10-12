#include "strategy/order_fail_retry.hpp"

#include "log/strategy_store.hpp"
#include "strategy/hk_short_params.hpp"
#include "strategy/symbol_tradability.hpp"
#include "trade/order_errors.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace mmtrade {
namespace strategy {

namespace {

std::string UpperCopy(const std::string &s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return out;
}

double EnvOrDefault(const char *env_key, double cfg_val, double fallback) {
  const char *raw = std::getenv(env_key);
  if (raw != NULL && raw[0] != '\0') {
    try {
      return std::max(0.0, std::stod(raw));
    } catch (...) {
    }
  }
  if (cfg_val >= 0.0) {
    return cfg_val;
  }
  return fallback;
}

int RetryMax(const config::StrategyConfig &cfg) {
  return cfg.order_retry_max >= 0 ? cfg.order_retry_max : 3;
}

}  // namespace

double OrderCancelRetryWaitSec(const config::StrategyConfig &cfg) {
  return EnvOrDefault("STRATEGY_ORDER_CANCEL_RETRY_WAIT_SEC", cfg.order_cancel_retry_wait_sec, 2.0);
}

double OrderLimitPriceRetryWaitSec(const config::StrategyConfig &cfg) {
  return EnvOrDefault("STRATEGY_ORDER_LIMIT_PRICE_RETRY_WAIT_SEC",
                      cfg.order_limit_price_retry_wait_sec, 0.5);
}

double OrderTooFastRetryWaitSec(const config::StrategyConfig &cfg) {
  return EnvOrDefault("STRATEGY_ORDER_TOO_FAST_RETRY_WAIT_SEC", cfg.order_too_fast_retry_wait_sec,
                      1.0);
}

double OrderRateLimitRetryWaitSec(const config::StrategyConfig &cfg) {
  return EnvOrDefault("STRATEGY_ORDER_RATE_LIMIT_RETRY_WAIT_SEC",
                      cfg.order_rate_limit_retry_wait_sec, 2.0);
}

double OrderFailRetryWaitSec(const config::StrategyConfig &cfg, const std::string &status,
                             const std::string &fail_detail) {
  using trade::IsCancelledAllDetail;
  using trade::IsCancelledAllStatus;
  using trade::IsOperatingTooFastErr;
  using trade::IsOrderRateLimitErr;
  using trade::IsRetryableInvalidPriceFail;
  using trade::IsRetryableLimitPriceFail;
  using trade::IsRetryableTryAgainLater;

  if (IsCancelledAllStatus(status) || IsCancelledAllDetail(fail_detail)) {
    return OrderCancelRetryWaitSec(cfg);
  }
  if (IsOrderRateLimitErr(fail_detail)) {
    return OrderRateLimitRetryWaitSec(cfg);
  }
  if (IsOperatingTooFastErr(fail_detail)) {
    return OrderTooFastRetryWaitSec(cfg);
  }
  if (IsRetryableLimitPriceFail(fail_detail) || IsRetryableInvalidPriceFail(fail_detail) ||
      IsRetryableTryAgainLater(fail_detail)) {
    return OrderLimitPriceRetryWaitSec(cfg);
  }
  return OrderCancelRetryWaitSec(cfg);
}

OrderTypeRetryChoice ResolveEntryOrderTypeForRetry(SymbolLoopState *st,
                                                   const config::StrategyConfig &cfg) {
  OrderTypeRetryChoice out;
  if (st == NULL) {
    out.order_type = "NORMAL";
    return out;
  }
  const bool hk_short =
      IsHkShortOpenEntry(st->sym.code, st->sym.entry_side, st->sym.entry_side);
  if (st->entry_force_market_next && !hk_short) {
    st->entry_force_market_next = false;
    return OrderTypeRetryChoice("MARKET", true);
  }
  if (st->entry_force_market_next && hk_short) {
    st->entry_force_market_next = false;
  }
  const std::string base = UpperCopy(st->sym.order_type.empty() ? "NORMAL" : st->sym.order_type);
  out.order_type = st->sym.order_type.empty() ? "NORMAL" : st->sym.order_type;
  const int fail_n = st->entry_retry_failed_count;
  const int retry_max = RetryMax(cfg);
  if (fail_n >= retry_max &&
      (base == "SIM_MARKET" || base == "NORMAL" || base == "LIMIT") && !hk_short) {
    return OrderTypeRetryChoice("MARKET", true);
  }
  return out;
}

OrderTypeRetryChoice ResolveExitOrderTypeForRetry(SymbolLoopState *st,
                                                  const config::StrategyConfig &cfg) {
  OrderTypeRetryChoice out;
  if (st == NULL) {
    out.order_type = "NORMAL";
    return out;
  }
  if (st->exit_force_market_next) {
    st->exit_force_market_next = false;
    return OrderTypeRetryChoice("MARKET", true);
  }
  const std::string base = UpperCopy(st->sym.order_type.empty() ? "NORMAL" : st->sym.order_type);
  out.order_type = st->sym.order_type.empty() ? "NORMAL" : st->sym.order_type;
  const int fail_n = st->exit_retry_failed_count;
  const int retry_max = RetryMax(cfg);
  if (fail_n >= retry_max && (base == "SIM_MARKET" || base == "NORMAL" || base == "LIMIT")) {
    return OrderTypeRetryChoice("MARKET", true);
  }
  return out;
}

void SetTradingFatal(SymbolLoopState *st, const std::string &fatal_code,
                     const std::string &detail, log::StrategyStore *store,
                     const std::string &log_key, const std::string &log_line) {
  if (st == NULL) {
    return;
  }
  st->entry_blocked = true;
  st->trading_fatal_code = fatal_code;
  st->trading_fatal_detail = detail.size() > 480 ? detail.substr(0, 480) : detail;
  if (store != NULL) {
    store->LogEvent("ERROR", log_key, log_line);
  }
}

OrderFailBumpResult BumpEntryOrderFail(SymbolLoopState *st, const config::StrategyConfig &cfg,
                                       double now_mono, const std::string &fail_detail,
                                       const std::string &oid, const std::string &status,
                                       log::StrategyStore *store,
                                       const std::string &immediate_fatal_code) {
  if (st == NULL) {
    return OrderFailBumpResult::kFatal;
  }
  st->entry_await_fill = false;

  if (IsWaitableSymbolNotTradableFail(fail_detail)) {
    const double wait_sec = SymbolTradabilityPollSec();
    st->place_cooldown_until_mono = now_mono + wait_sec;
    st->symbol_tradable = false;
    const std::string reason = trade::OrderFailDetailForLog(status, fail_detail);
    st->symbol_tradable_reason = reason.size() > 200 ? reason.substr(0, 200) : reason;
    if (store != NULL) {
      std::ostringstream oss;
      oss << "entry_symbol_not_tradable_wait " << st->sym.code << " wait_sec=" << wait_sec
          << " reason=" << (st->symbol_tradable_reason.empty() ? "symbol_not_tradable"
                                                              : st->symbol_tradable_reason);
      store->LogEvent("WARN", "entry_symbol_not_tradable_wait", oss.str());
    }
    return OrderFailBumpResult::kContinue;
  }

  if (!trade::IsOrderFailRetriable(status, fail_detail)) {
    const std::string log_reason = trade::OrderFailDetailForLog(status, fail_detail);
    std::ostringstream line;
    line << "entry_order_fatal | code=" << st->sym.code << " status=" << (status.empty() ? "-" : status)
         << " reason=" << log_reason;
    SetTradingFatal(st, immediate_fatal_code, log_reason.empty() ? "entry_order_failed" : log_reason,
                    store, "entry_order_fatal", line.str());
    return OrderFailBumpResult::kFatal;
  }

  st->entry_retry_failed_count += 1;
  const int retry_max = RetryMax(cfg);
  if (trade::IsRetryableShortSellAskPriceFail(fail_detail) && st->entry_retry_failed_count >= 2) {
    if (!IsHkShortOpenEntry(st->sym.code, st->sym.entry_side, st->sym.entry_side)) {
      st->entry_force_market_next = true;
    }
  }

  const double wait_sec = OrderFailRetryWaitSec(cfg, status, fail_detail);
  st->place_cooldown_until_mono = now_mono + wait_sec;
  const std::string log_reason = trade::OrderFailDetailForLog(status, fail_detail);

  std::string event_key = "entry_order_retry_scheduled";
  if (trade::IsCancelledAllStatus(status) || trade::IsCancelledAllDetail(fail_detail)) {
    event_key = "entry_cancel_retry_scheduled";
  } else if (trade::IsRetryableShortSellAskPriceFail(fail_detail)) {
    event_key = "entry_short_ask_price_retry_scheduled";
  } else if (trade::IsRetryableLimitPriceFail(fail_detail)) {
    event_key = "entry_limit_price_retry_scheduled";
  } else if (trade::IsRetryableInvalidPriceFail(fail_detail)) {
    event_key = "entry_invalid_price_retry_scheduled";
  } else if (trade::IsOperatingTooFastErr(fail_detail)) {
    event_key = "entry_too_fast_retry_scheduled";
  } else if (trade::IsOrderRateLimitErr(fail_detail)) {
    event_key = "entry_rate_limit_retry_scheduled";
  } else if (trade::IsRetryableTryAgainLater(fail_detail)) {
    event_key = "entry_try_again_market_retry_scheduled";
    st->entry_force_market_next = true;
  }

  if (store != NULL) {
    std::ostringstream oss;
    oss << event_key << " " << st->sym.code << " oid=" << (oid.empty() ? "-" : oid)
        << " wait_sec=" << wait_sec << " retry=" << st->entry_retry_failed_count << "/"
        << (retry_max + 1) << " err=" << log_reason;
    store->LogEvent("WARN", event_key, oss.str());
  }

  if (st->entry_retry_failed_count > retry_max) {
    std::ostringstream line;
    line << "entry_retry_exceeded_stop | code=" << st->sym.code << " retry_max=" << retry_max
         << " => stop";
    SetTradingFatal(st, "ENTRY_RETRY_EXCEEDED",
                    fail_detail.empty() ? ("retry_max=" + std::to_string(retry_max)) : fail_detail,
                    store, "entry_retry_exceeded_stop", line.str());
    return OrderFailBumpResult::kFatal;
  }
  return OrderFailBumpResult::kContinue;
}

OrderFailBumpResult BumpExitOrderFail(SymbolLoopState *st, const config::StrategyConfig &cfg,
                                      double now_mono, const std::string &fail_detail,
                                      const std::string &oid, const std::string &status,
                                      log::StrategyStore *store,
                                      const std::string &immediate_fatal_code) {
  if (st == NULL) {
    return OrderFailBumpResult::kFatal;
  }
  st->exit_await_fill = false;

  if (IsWaitableSymbolNotTradableFail(fail_detail)) {
    const double wait_sec = SymbolTradabilityPollSec();
    st->place_cooldown_until_mono = now_mono + wait_sec;
    st->symbol_tradable = false;
    const std::string reason = trade::OrderFailDetailForLog(status, fail_detail);
    st->symbol_tradable_reason = reason.size() > 200 ? reason.substr(0, 200) : reason;
    if (store != NULL) {
      std::ostringstream oss;
      oss << "exit_symbol_not_tradable_wait " << st->sym.code << " wait_sec=" << wait_sec
          << " reason=" << (st->symbol_tradable_reason.empty() ? "symbol_not_tradable"
                                                              : st->symbol_tradable_reason);
      store->LogEvent("WARN", "exit_symbol_not_tradable_wait", oss.str());
    }
    return OrderFailBumpResult::kContinue;
  }

  if (!trade::IsOrderFailRetriable(status, fail_detail)) {
    const std::string log_reason = trade::OrderFailDetailForLog(status, fail_detail);
    std::ostringstream line;
    line << "exit_order_fatal | code=" << st->sym.code << " status=" << (status.empty() ? "-" : status)
         << " reason=" << log_reason;
    SetTradingFatal(st, immediate_fatal_code, log_reason.empty() ? "exit_order_failed" : log_reason,
                    store, "exit_order_fatal", line.str());
    return OrderFailBumpResult::kFatal;
  }

  st->exit_retry_failed_count += 1;
  const int retry_max = RetryMax(cfg);
  const double wait_sec = OrderFailRetryWaitSec(cfg, status, fail_detail);
  st->place_cooldown_until_mono = now_mono + wait_sec;
  const std::string log_reason = trade::OrderFailDetailForLog(status, fail_detail);

  std::string event_key = "exit_order_retry_scheduled";
  if (trade::IsCancelledAllStatus(status) || trade::IsCancelledAllDetail(fail_detail)) {
    event_key = "exit_cancel_retry_scheduled";
  } else if (trade::IsRetryableLimitPriceFail(fail_detail)) {
    event_key = "exit_limit_price_retry_scheduled";
  } else if (trade::IsRetryableInvalidPriceFail(fail_detail)) {
    event_key = "exit_invalid_price_retry_scheduled";
  } else if (trade::IsOperatingTooFastErr(fail_detail)) {
    event_key = "exit_too_fast_retry_scheduled";
  } else if (trade::IsOrderRateLimitErr(fail_detail)) {
    event_key = "exit_rate_limit_retry_scheduled";
  } else if (trade::IsRetryableTryAgainLater(fail_detail)) {
    event_key = "exit_try_again_market_retry_scheduled";
    st->exit_force_market_next = true;
  }

  if (store != NULL) {
    std::ostringstream oss;
    oss << event_key << " " << st->sym.code << " oid=" << (oid.empty() ? "-" : oid)
        << " wait_sec=" << wait_sec << " retry=" << st->exit_retry_failed_count << "/"
        << (retry_max + 1) << " err=" << log_reason;
    store->LogEvent("WARN", event_key, oss.str());
  }

  if (st->exit_retry_failed_count > retry_max) {
    std::ostringstream line;
    line << "exit_retry_exceeded_stop | code=" << st->sym.code << " retry_max=" << retry_max
         << " => stop";
    SetTradingFatal(st, "EXIT_RETRY_EXCEEDED",
                    fail_detail.empty() ? ("retry_max=" + std::to_string(retry_max)) : fail_detail,
                    store, "exit_retry_exceeded_stop", line.str());
    return OrderFailBumpResult::kFatal;
  }
  return OrderFailBumpResult::kContinue;
}

}  // namespace strategy
}  // namespace mmtrade
