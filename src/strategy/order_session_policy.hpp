#pragma once

#include <string>

namespace mmtrade {
namespace strategy {

std::string NormalizeOrderSession(const std::string &raw);
// Paper/SIMULATE: ALL is downgraded to RTH for PlaceOrder (broker rejects ALL/overnight).
std::string OrderSessionForPlace(const std::string &order_session, bool is_paper);
bool PhaseAllowsTrading(const std::string &order_session, const std::string &market_phase);
// pre / rth / post / overnight / unknown. Accepts OpenD names or QotMarketState ints.
std::string MarketPhaseFromStateName(const std::string &state_name);
std::string MarketPhaseFromStateCode(int market_state);
// kind = "entry" | "exit". session=ALL splits RTH vs overnight fatal codes.
std::string RetryExceededFatalCode(const std::string &order_session,
                                   const std::string &market_phase,
                                   const std::string &kind);
// Empty return = do not set SDK session (explicit NONE).
std::string SdkSessionStringForPlace(const std::string &order_session, bool is_paper = false);
bool FillOutsideRthForSession(const std::string &order_session, bool is_paper = false);
bool SessionAllowsMarketOrder(const std::string &order_session,
                              const std::string &market_phase = "");
std::string ClampOrderTypeForSession(const std::string &order_session,
                                     const std::string &order_type,
                                     const std::string &market_phase = "");
bool EffectiveAutoStopAfterClose(bool template_auto_stop, const std::string &order_session);

}  // namespace strategy
}  // namespace mmtrade
