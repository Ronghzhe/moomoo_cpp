#include "config/strategy_loader.hpp"
#include "engine/staged_take_profit_util.hpp"
#include "strategy/book_guard.hpp"
#include "strategy/hk_short_params.hpp"
#include "strategy/strategy_judge.hpp"
#include "strategy/strategy_runner.hpp"
#include "strategy/symbol_runtime.hpp"
#include "strategy/tick_builder.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string ReadFile(const std::string &path) {
  std::ifstream ifs(path.c_str());
  std::ostringstream oss;
  oss << ifs.rdbuf();
  return oss.str();
}

std::string FixturePath(const std::string &name) {
  return std::string(MOOMOO_FIXTURES_DIR) + "/" + name;
}

std::string ReplayPath(const std::string &name) {
  return std::string(MOOMOO_REPLAY_DIR) + "/" + name;
}

std::string GoldenL2Path(const std::string &name) {
  return std::string(MOOMOO_GOLDEN_L2_DIR) + "/" + name;
}

nlohmann::json LoadJsonFile(const std::string &path) {
  return nlohmann::json::parse(ReadFile(path));
}

void ApplyExitOverrides(mmtrade::config::SymbolConfig *sym,
                        const nlohmann::json &overrides) {
  if (sym == NULL || overrides.is_null()) {
    return;
  }
  const nlohmann::json enable = overrides.value("enable_rule_ids", nlohmann::json::array());
  const nlohmann::json disable = overrides.value("disable_rule_ids", nlohmann::json::array());
  for (size_t i = 0; i < sym->exit_strategies.size(); ++i) {
    mmtrade::config::ExitStrategyRule &rule = sym->exit_strategies[i];
    for (size_t j = 0; j < enable.size(); ++j) {
      if (rule.rule_id == enable[j].get<std::string>()) {
        rule.enabled = true;
      }
    }
    for (size_t j = 0; j < disable.size(); ++j) {
      if (rule.rule_id == disable[j].get<std::string>()) {
        rule.enabled = false;
      }
    }
  }
}

mmtrade::strategy::StrategyTickContext BuildTickFromJson(
    const mmtrade::config::SymbolConfig &sym, const nlohmann::json &tick) {
  if (tick.contains("bid_1") || tick.contains("ask_1")) {
    mmtrade::strategy::BookTick book;
    book.valid = true;
    book.bid_1 = tick.value("bid_1", 0.0);
    book.ask_1 = tick.value("ask_1", 0.0);
    mmtrade::config::SymbolConfig sym_copy = sym;
    if (tick.contains("gain_fraction")) {
      sym_copy.opened_price = sym.opened_price > 0 ? sym.opened_price : 100.0;
    }
    mmtrade::strategy::StrategyTickContext ctx =
        mmtrade::strategy::BuildTickContext(sym_copy, book, tick.value("now", 0.0), sym.code);
    if (tick.contains("gain_fraction")) {
      ctx.has_gain_fraction = true;
      ctx.gain_fraction = tick["gain_fraction"].get<double>();
    }
    return ctx;
  }
  mmtrade::strategy::StrategyTickContext ctx;
  ctx.now = tick.value("now", 0.0);
  if (tick.contains("gain_fraction")) {
    ctx.has_gain_fraction = true;
    ctx.gain_fraction = tick["gain_fraction"].get<double>();
  }
  return ctx;
}

nlohmann::json RunStrategyJudgeScenario(const nlohmann::json &replay) {
  const mmtrade::config::StrategyConfig cfg =
      mmtrade::config::LoadConfig(FixturePath(replay.at("fixture").get<std::string>()));
  const size_t sym_idx = replay.value("symbol_index", 0);
  if (sym_idx >= cfg.symbols.size()) {
    throw std::runtime_error("symbol_index out of range");
  }
  mmtrade::config::SymbolConfig sym = cfg.symbols[sym_idx];
  ApplyExitOverrides(&sym, replay.value("exit_overrides", nlohmann::json()));

  nlohmann::json steps = nlohmann::json::array();
  mmtrade::strategy::SymbolEvalRuntime rt;
  for (size_t i = 0; i < replay.at("steps").size(); ++i) {
    const nlohmann::json step = replay.at("steps").at(i);
    const mmtrade::strategy::StrategyTickContext tick = BuildTickFromJson(sym, step.at("tick"));
    const mmtrade::strategy::SymbolJudgeOutcome out =
        mmtrade::strategy::RunSymbolJudge(sym, tick, &rt, NULL);
    steps.push_back({{"label", step.at("label")},
                     {"entry_hit", out.entry_hit},
                     {"entry_rule_id", out.entry_hit ? out.entry_rule_id : ""},
                     {"exit_hit", out.exit_hit},
                     {"exit_rule_id", out.exit_hit ? out.exit_rule_id : ""}});
  }
  return {{"id", replay.at("id")}, {"steps", steps}};
}

nlohmann::json RunHkShortBandScenario(const nlohmann::json &replay) {
  nlohmann::json cases = nlohmann::json::array();
  for (size_t i = 0; i < replay.at("cases").size(); ++i) {
    const nlohmann::json c = replay.at("cases").at(i);
    const double ask = c.at("ask").get<double>();
    double lo = 0.0;
    double hi = 0.0;
    mmtrade::strategy::HkShortAskBand(ask, &lo, &hi);
    nlohmann::json out = {{"ask", ask}, {"band_lo", lo}, {"band_hi", hi}};
    if (c.contains("bid")) {
      const double bid = c.at("bid").get<double>();
      out["bid"] = bid;
      out["bid_in_band"] = mmtrade::strategy::HkShortBidInAskBand(bid, ask);
    }
    if (c.contains("ask_in_band")) {
      const double px = c.at("ask_in_band").get<double>();
      out["ask_in_band"] = px;
      out["ask_in_original_band"] = mmtrade::strategy::HkShortAskInOriginalBand(px, lo, hi);
    }
    cases.push_back(out);
  }
  return {{"id", replay.at("id")}, {"cases", cases}};
}

nlohmann::json RunStagedTakeProfitScenario(const nlohmann::json &replay) {
  nlohmann::json cases = nlohmann::json::array();
  for (size_t i = 0; i < replay.at("cases").size(); ++i) {
    const nlohmann::json c = replay.at("cases").at(i);
    const double baseline = mmtrade::engine::ResolveStagedBaselineQty(
        c.at("opened_qty").get<double>(), c.at("initial_qty").get<double>());
    std::vector<double> fractions;
    for (size_t j = 0; j < c.at("fractions").size(); ++j) {
      fractions.push_back(c.at("fractions").at(j).get<double>());
    }
    const std::vector<double> qtys =
        mmtrade::engine::AllocateTrancheQtys(baseline, fractions, c.at("lot_size").get<int>());
    double sum_qty = 0.0;
    for (size_t j = 0; j < qtys.size(); ++j) {
      sum_qty += qtys[j];
    }
    nlohmann::json qty_json = nlohmann::json::array();
    for (size_t j = 0; j < qtys.size(); ++j) {
      qty_json.push_back(qtys[j]);
    }
    cases.push_back({{"baseline_qty", baseline}, {"qtys", qty_json}, {"sum_qty", sum_qty}});
  }
  return {{"id", replay.at("id")}, {"cases", cases}};
}

nlohmann::json RunBookGuardRefScenario(const nlohmann::json &replay) {
  nlohmann::json cases = nlohmann::json::array();
  for (size_t i = 0; i < replay.at("cases").size(); ++i) {
    const nlohmann::json c = replay.at("cases").at(i);
    if (c.contains("ref_px")) {
      const std::string code = c.value("code", "US.AMD");
      const std::string side = c.value("side", "SELL");
      const double px = mmtrade::strategy::EmergencyLimitSubmitPx(
          code, side, c.at("ref_px").get<double>());
      cases.push_back({{"submit_px", px}});
      continue;
    }
    const mmtrade::strategy::EmergencyRefResult r = mmtrade::strategy::EmergencyRefPrice(
        c.at("side").get<std::string>(), c.value("bid_1", 0.0), c.value("ask_1", 0.0),
        c.at("opened_price").get<double>(), c.value("last_price", 0.0));
    cases.push_back({{"price", r.price}, {"source", r.source}});
  }
  return {{"id", replay.at("id")}, {"cases", cases}};
}

nlohmann::json RunReplayScenario(const nlohmann::json &replay) {
  const std::string module = replay.at("module").get<std::string>();
  if (module == "strategy_judge") {
    return RunStrategyJudgeScenario(replay);
  }
  if (module == "hk_short_band") {
    return RunHkShortBandScenario(replay);
  }
  if (module == "staged_take_profit") {
    return RunStagedTakeProfitScenario(replay);
  }
  if (module == "book_guard_ref") {
    return RunBookGuardRefScenario(replay);
  }
  throw std::runtime_error("unknown replay module: " + module);
}

void ExpectJsonEqualWithTolerance(const nlohmann::json &got, const nlohmann::json &expect,
                                  const std::string &scenario_id) {
  if (got == expect) {
    return;
  }
  if (got.is_number() && expect.is_number()) {
    EXPECT_NEAR(got.get<double>(), expect.get<double>(), 0.01) << scenario_id;
    return;
  }
  if (got.is_array() && expect.is_array()) {
    ASSERT_EQ(got.size(), expect.size()) << scenario_id;
    for (size_t i = 0; i < got.size(); ++i) {
      ExpectJsonEqualWithTolerance(got.at(i), expect.at(i), scenario_id);
    }
    return;
  }
  if (got.is_object() && expect.is_object()) {
    for (nlohmann::json::const_iterator it = expect.begin(); it != expect.end(); ++it) {
      ASSERT_TRUE(got.contains(it.key())) << scenario_id << " missing key " << it.key();
      ExpectJsonEqualWithTolerance(got.at(it.key()), it.value(), scenario_id);
    }
    return;
  }
  EXPECT_EQ(got, expect) << scenario_id;
}

void RunL2ScenarioFile(const std::string &scenario_id) {
  const nlohmann::json replay = LoadJsonFile(ReplayPath(scenario_id + ".json"));
  const nlohmann::json golden = LoadJsonFile(GoldenL2Path(scenario_id + ".golden.json"));
  const nlohmann::json got = RunReplayScenario(replay);
  ExpectJsonEqualWithTolerance(got, golden, scenario_id);
}

std::vector<std::string> LoadManifestScenarioIds() {
  const nlohmann::json manifest = LoadJsonFile(ReplayPath("l2_manifest.json"));
  std::vector<std::string> ids;
  for (size_t i = 0; i < manifest.at("scenarios").size(); ++i) {
    ids.push_back(manifest.at("scenarios").at(i).get<std::string>());
  }
  return ids;
}

}  // namespace

TEST(L2GoldenReplay, ManifestScenariosMatchPythonGolden) {
  const std::vector<std::string> ids = LoadManifestScenarioIds();
  ASSERT_GE(ids.size(), 5u);
  for (size_t i = 0; i < ids.size(); ++i) {
    SCOPED_TRACE(ids[i]);
    RunL2ScenarioFile(ids[i]);
  }
}
