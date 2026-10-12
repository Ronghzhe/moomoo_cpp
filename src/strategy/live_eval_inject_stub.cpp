#include "strategy/live_eval_inject.hpp"

namespace mmtrade {
namespace strategy {

void InjectLiveEntryEval(const config::Condition &, LiveEvalCompute *, const StrategyTickContext &,
                         engine::EntryEvalBundle *) {}

void InjectLiveExitEval(const config::Condition &, LiveEvalCompute *, const StrategyTickContext &,
                        engine::ExitEvalBundle *) {}

}  // namespace strategy
}  // namespace mmtrade
