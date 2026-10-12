#include "engine/staged_take_profit_util.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

namespace mmtrade {
namespace engine {

namespace {

double FieldValueOrMid(const FieldMap &fields, const std::string &price_field) {
  if (price_field == "mid") {
    FieldMap::const_iterator b = fields.find("bid_1");
    FieldMap::const_iterator a = fields.find("ask_1");
    if (b != fields.end() && a != fields.end()) {
      return (b->second + a->second) / 2.0;
    }
    return 0.0;
  }
  FieldMap::const_iterator it = fields.find(price_field);
  if (it == fields.end()) {
    return 0.0;
  }
  return it->second;
}

}  // namespace

double ResolveStagedBaselineQty(double opened_qty, double initial_qty) {
  if (initial_qty <= 0.0) {
    return opened_qty;
  }
  return std::max(initial_qty, opened_qty);
}

std::vector<double> AllocateTrancheQtys(double initial_qty, const std::vector<double> &fractions,
                                        int lot_size) {
  const int L = std::max(1, lot_size);
  const int n = static_cast<int>(fractions.size());
  std::vector<double> out(n, 0.0);
  if (n == 0 || initial_qty <= 0.0) {
    return out;
  }
  const int total_u = static_cast<int>(std::floor(initial_qty / L + 1e-12));
  if (total_u <= 0) {
    return out;
  }

  std::vector<double> ideals(n);
  std::vector<int> bases(n);
  for (int i = 0; i < n; ++i) {
    ideals[i] = total_u * fractions[static_cast<size_t>(i)];
    bases[i] = static_cast<int>(std::floor(ideals[i] + 1e-12));
  }
  int rem = total_u;
  for (int i = 0; i < n; ++i) {
    rem -= bases[i];
  }
  if (rem > 0) {
    std::vector<int> order(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
      order[static_cast<size_t>(i)] = i;
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) {
      const double da = ideals[static_cast<size_t>(a)] - bases[static_cast<size_t>(a)];
      const double db = ideals[static_cast<size_t>(b)] - bases[static_cast<size_t>(b)];
      if (da != db) {
        return da > db;
      }
      return a > b;
    });
    for (int k = 0; k < rem; ++k) {
      bases[static_cast<size_t>(order[static_cast<size_t>(k)])] += 1;
    }
  }

  for (int i = 0; i < n; ++i) {
    out[static_cast<size_t>(i)] = static_cast<double>(bases[i] * L);
  }
  const double leftover = initial_qty - static_cast<double>(total_u * L);
  if (leftover > 1e-12 && n > 0) {
    out[static_cast<size_t>(n - 1)] += leftover;
  }
  return out;
}

EvalResult EvaluateStagedTakeProfit(const StagedTakeProfitCondition &cond,
                                    const StagedTpEvalContext &ctx) {
  EvalResult out;
  if (ctx.runtime == NULL) {
    out.handled = false;
    out.hit = false;
    out.reason = "missing_dependency";
    return out;
  }

  std::string price_field = cond.price_field.empty() ? "bid_1" : cond.price_field;
  const double lv = FieldValueOrMid(ctx.fields, price_field);
  const double entry_px = ctx.sym.opened_price;
  if (lv <= 0.0 || entry_px <= 0.0) {
    out.handled = true;
    out.hit = false;
    out.reason = "invalid_price_or_entry";
    return out;
  }

  if (cond.targets.empty() || cond.targets.size() != cond.fractions.size()) {
    out.handled = false;
    out.hit = false;
    out.reason = "invalid_config";
    return out;
  }

  const double gain = (lv - entry_px) / entry_px;
  const double initial_qty = ResolveStagedBaselineQty(ctx.sym.opened_qty, ctx.sym.initial_qty);
  const double available =
      std::max(0.0, ctx.sym.opened_qty - ctx.sym.closed_qty);
  if (available <= 0.0 || initial_qty <= 0.0) {
    out.handled = true;
    out.hit = false;
    out.reason = "no_available_or_initial";
    return out;
  }

  const int lot_size = std::max(1, ctx.runtime->lot_size);
  const std::vector<double> tranche_qtys =
      AllocateTrancheQtys(initial_qty, cond.fractions, lot_size);

  double plan_qty = 0.0;
  for (size_t i = 0; i < cond.targets.size(); ++i) {
    if (gain < cond.targets[i]) {
      continue;
    }
    std::ostringstream key;
    key << ctx.rule_id << ":" << ctx.cond_idx << ":" << i;
    const std::string k = key.str();
    const double filled_i = ctx.runtime->filled_qty[k];
    const double target_qty_i = i < tranche_qtys.size() ? tranche_qtys[i] : 0.0;
    const double need_i = std::max(0.0, target_qty_i - filled_i);
    if (need_i <= 1e-12) {
      continue;
    }
    plan_qty = std::min(need_i, available);
    if (plan_qty > 1e-12) {
      break;
    }
  }

  out.handled = true;
  out.has_left_value = true;
  out.left_value = lv;
  out.has_right_value = true;
  out.right_value = entry_px;
  if (plan_qty <= 1e-12) {
    out.hit = false;
    out.reason = "no_plan_qty";
  } else {
    out.hit = true;
    out.reason = "staged_take_profit";
  }
  return out;
}

}  // namespace engine
}  // namespace mmtrade
