#include "strategy/order_session_policy.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace mmtrade {
namespace strategy {

namespace {

std::string UpperTrim(const std::string &s) {
  std::string t = s;
  t.erase(std::remove_if(t.begin(), t.end(), [](unsigned char c) { return std::isspace(c); }),
          t.end());
  std::transform(t.begin(), t.end(), t.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return t;
}

}  // namespace

std::string NormalizeOrderSession(const std::string &raw) {
  const std::string u = UpperTrim(raw);
  if (u.empty()) {
    return "RTH";
  }
  if (u == "RTH" || u == "ALL" || u == "OVERNIGHT" || u == "NONE") {
    return u;
  }
  throw std::runtime_error("order_session must be one of RTH, ALL, OVERNIGHT, NONE");
}

std::string MarketPhaseFromStateName(const std::string &state_name) {
  std::string s = UpperTrim(state_name);
  if (s == "MORNING" || s == "AFTERNOON") {
    return "rth";
  }
  if (s == "REST") {
    return "pre";
  }
  if (s == "PRE_MARKET_BEGIN" || s == "PREMARKETBEGIN" || s == "PRE_MARKET_END" ||
      s == "PREMARKETEND" || s == "WAITING_OPEN" || s == "WAITINGOPEN" || s == "AUCTION") {
    return "pre";
  }
  if (s == "OVERNIGHT" || s == "OVERNIGHT_BEGIN" || s == "OVERNIGHTBEGIN" ||
      s == "OVERNIGHT_END" || s == "OVERNIGHTEND" || s == "NIGHT" || s == "NIGHT_OPEN" ||
      s == "NIGHTOPEN" || s == "NIGHT_END" || s == "NIGHTEND") {
    return "overnight";
  }
  if (s == "AFTER_HOURS_BEGIN" || s == "AFTERHOURSBEGIN" || s == "AFTER_HOURS_END" ||
      s == "AFTERHOURSEND" || s == "CLOSED" || s == "HK_CAS" || s == "HKCAS") {
    return "post";
  }
  return "unknown";
}

std::string MarketPhaseFromStateCode(int market_state) {
  // Qot_Common::QotMarketState (keep policy free of protobuf).
  switch (market_state) {
    case 3:   // Morning
    case 5:   // Afternoon
      return "rth";
    case 1:   // Auction
    case 2:   // WaitingOpen
    case 4:   // Rest (HK lunch)
    case 8:   // PreMarketBegin
    case 9:   // PreMarketEnd
      return "pre";
    case 13:  // NightOpen
    case 14:  // NightEnd
    case 32:  // NIGHT
    case 33:  // OVERNIGHT_BEGIN
    case 34:  // OVERNIGHT_END
    case 37:  // OVERNIGHT
      return "overnight";
    case 6:   // Closed
    case 10:  // AfterHoursBegin
    case 11:  // AfterHoursEnd
    case 19:  // HkCas
      return "post";
    default:
      return "unknown";
  }
}

std::string RetryExceededFatalCode(const std::string &order_session,
                                   const std::string &market_phase,
                                   const std::string &kind) {
  std::string prefix = "ENTRY";
  std::string k = kind;
  std::transform(k.begin(), k.end(), k.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (k == "exit") {
    prefix = "EXIT";
  }
  std::string sess = "RTH";
  try {
    sess = NormalizeOrderSession(order_session);
  } catch (...) {
    sess = "RTH";
  }
  std::string phase = market_phase;
  std::transform(phase.begin(), phase.end(), phase.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (sess == "ALL") {
    if (phase == "rth") {
      return prefix + "_RETRY_EXCEEDED_RTH";
    }
    return prefix + "_RETRY_EXCEEDED_OVERNIGHT";
  }
  return prefix + "_RETRY_EXCEEDED";
}

bool PhaseAllowsTrading(const std::string &order_session, const std::string &market_phase) {
  const std::string sess = NormalizeOrderSession(order_session);
  std::string phase = market_phase;
  std::transform(phase.begin(), phase.end(), phase.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (sess == "RTH") {
    return phase == "rth";
  }
  if (sess == "ALL") {
    return phase == "pre" || phase == "rth" || phase == "post";
  }
  if (sess == "OVERNIGHT") {
    return phase == "overnight" || phase == "post";
  }
  return true;
}

std::string OrderSessionForPlace(const std::string &order_session, bool is_paper) {
  const std::string sess = NormalizeOrderSession(order_session);
  if (is_paper && sess == "ALL") {
    return "RTH";
  }
  return sess;
}

std::string SdkSessionStringForPlace(const std::string &order_session, bool is_paper) {
  const std::string sess = OrderSessionForPlace(order_session, is_paper);
  if (sess == "NONE") {
    return "";
  }
  return sess;
}

bool FillOutsideRthForSession(const std::string &order_session, bool is_paper) {
  return OrderSessionForPlace(order_session, is_paper) == "ALL";
}

bool SessionAllowsMarketOrder(const std::string &order_session,
                              const std::string &market_phase) {
  const std::string sess = NormalizeOrderSession(order_session.empty() ? "RTH" : order_session);
  std::string phase = market_phase;
  std::transform(phase.begin(), phase.end(), phase.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (sess == "OVERNIGHT") {
    return false;
  }
  if (phase == "pre" || phase == "post" || phase == "overnight") {
    return false;
  }
  if (phase == "rth") {
    return true;
  }
  return sess == "RTH" || sess == "NONE";
}

std::string ClampOrderTypeForSession(const std::string &order_session,
                                     const std::string &order_type,
                                     const std::string &market_phase) {
  std::string ot = UpperTrim(order_type.empty() ? "NORMAL" : order_type);
  if (ot == "MARKET" && !SessionAllowsMarketOrder(order_session, market_phase)) {
    return "NORMAL";
  }
  return order_type.empty() ? "NORMAL" : order_type;
}

bool EffectiveAutoStopAfterClose(bool template_auto_stop, const std::string &order_session) {
  if (!template_auto_stop) {
    return false;
  }
  return NormalizeOrderSession(order_session) == "RTH";
}

}  // namespace strategy
}  // namespace mmtrade
