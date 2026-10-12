#pragma once

#include "config/strategy_types.hpp"
#include "engine/registry.hpp"

namespace mmtrade {
namespace config {

void FillExitEvalBundle(const Condition &cond, engine::ExitEvalBundle *out);
void FillEntryEvalBundle(const Condition &cond, engine::EntryEvalBundle *out);

}  // namespace config
}  // namespace mmtrade
