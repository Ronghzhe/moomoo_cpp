#pragma once

#include "engine/condition_types.hpp"

#include <map>
#include <string>

namespace mmtrade {
namespace engine {

double ResolveStagedBaselineQty(double opened_qty, double initial_qty);
std::vector<double> AllocateTrancheQtys(double initial_qty, const std::vector<double> &fractions,
                                        int lot_size);

struct StagedTpSymbolSnapshot {
  double opened_qty;
  double initial_qty;
  double opened_price;
  double closed_qty;

  StagedTpSymbolSnapshot()
      : opened_qty(0.0), initial_qty(0.0), opened_price(0.0), closed_qty(0.0) {}
};

struct StagedTpRuntime {
  int lot_size;
  std::map<std::string, double> filled_qty;

  StagedTpRuntime() : lot_size(1) {}
};

struct StagedTpEvalContext {
  StagedTpSymbolSnapshot sym;
  StagedTpRuntime *runtime;
  FieldMap fields;
  std::string rule_id;
  int cond_idx;

  StagedTpEvalContext() : runtime(NULL), cond_idx(-1) {}
};

EvalResult EvaluateStagedTakeProfit(const StagedTakeProfitCondition &cond,
                                    const StagedTpEvalContext &ctx);

}  // namespace engine
}  // namespace mmtrade
