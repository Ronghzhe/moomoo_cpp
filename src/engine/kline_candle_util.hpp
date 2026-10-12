#pragma once

#include "engine/condition_types.hpp"

namespace mmtrade {
namespace engine {

extern const char kKlineCandleRefBarOpen[];
extern const char kKlineCandleRefPrevClose[];

std::string NormalizeKlineCandleRef(const std::string &raw);
KlineCandleShapeResult EvaluateKlineCandleShape(const KlineCandleCondition &cond, double close,
                                                double bar_open, double prev_close);

}  // namespace engine
}  // namespace mmtrade
