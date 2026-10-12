#include "config/config_persist.hpp"
#include "config/strategy_loader.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

std::string TempDir() {
  char tmpl[] = "/tmp/moomoo_persist_test_XXXXXX";
  char *d = mkdtemp(tmpl);
  return d != NULL ? std::string(d) : std::string(".");
}

std::string WriteJson(const std::string &dir, const std::string &name, const std::string &body) {
  const std::string path = dir + "/" + name;
  std::ofstream ofs(path.c_str());
  ofs << body;
  return path;
}

std::string ReadFile(const std::string &path) {
  std::ifstream ifs(path.c_str());
  return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

std::vector<std::string> SymbolKeyOrder(const std::string &path) {
  nlohmann::ordered_json doc = nlohmann::ordered_json::parse(ReadFile(path));
  std::vector<std::string> keys;
  for (nlohmann::ordered_json::iterator it = doc.at("symbols").at(0).begin();
       it != doc.at("symbols").at(0).end(); ++it) {
    keys.push_back(it.key());
  }
  return keys;
}

std::vector<std::string> TopLevelKeyOrder(const std::string &path) {
  nlohmann::ordered_json doc = nlohmann::ordered_json::parse(ReadFile(path));
  std::vector<std::string> keys;
  for (nlohmann::ordered_json::iterator it = doc.begin(); it != doc.end(); ++it) {
    keys.push_back(it.key());
  }
  return keys;
}

}  // namespace

TEST(ConfigPersist, PersistUpdatesRuntimeFieldsInMainConfig) {
  const std::string dir = TempDir();
  const std::string main_path = WriteJson(
      dir, "cfg.json",
      R"({
  "version": 1,
  "client_name": "x",
  "strategy_name": "t",
  "mode": "paper",
  "opend": {"host":"127.0.0.1","port":11121},
  "account": {"trd_market":"HK"},
  "symbols": [{"code":"HK.02208","entry_side":"BUY","entry_qty":100,"status":0,"opened_qty":0,"opened_price":0,"closed_qty":0,"closed_price":0,"runtime_updated_at":"old"}]
})");

  mmtrade::config::SymbolConfig sym;
  sym.code = "HK.02208";
  sym.status = 2;
  sym.opened_qty = 200;
  sym.opened_price = 10.61;
  mmtrade::config::PersistStrategyConfigRuntime(main_path, sym);

  const nlohmann::json doc = nlohmann::json::parse(ReadFile(main_path));
  EXPECT_EQ(doc.at("symbols").at(0).at("status").get<int>(), 2);
  EXPECT_DOUBLE_EQ(doc.at("symbols").at(0).at("opened_qty").get<double>(), 200.0);
  EXPECT_DOUBLE_EQ(doc.at("symbols").at(0).at("opened_price").get<double>(), 10.61);
  EXPECT_NE(doc.at("symbols").at(0).at("runtime_updated_at").get<std::string>(), "old");

  const mmtrade::config::StrategyConfig cfg = mmtrade::config::LoadConfig(main_path);
  EXPECT_EQ(cfg.symbols[0].status, 2);
  EXPECT_DOUBLE_EQ(cfg.symbols[0].opened_qty, 200.0);
}

TEST(ConfigPersist, OrderPreservedOnPersist) {
  const std::string dir = TempDir();
  const std::string main_path = WriteJson(
      dir, "cfg.json",
      R"({
  "version": 1,
  "client_name": "x",
  "strategy_name": "t",
  "mode": "paper",
  "symbols": [{
    "task_id": "t1",
    "enabled": true,
    "code": "HK.02208",
    "entry_qty": 100,
    "entry_side": "BUY",
    "status": 0,
    "opened_qty": 0.0,
    "opened_price": 0.0,
    "closed_qty": 0.0,
    "closed_price": 0.0,
    "initial_qty": 100.0,
    "runtime_updated_at": "old"
  }],
  "opend": {"host":"127.0.0.1","port":11121},
  "account": {"trd_market":"HK"}
})");
  const std::vector<std::string> before = SymbolKeyOrder(main_path);
  const std::vector<std::string> before_top = TopLevelKeyOrder(main_path);
  ASSERT_GE(before.size(), 2u);
  EXPECT_EQ(before[0], "task_id");
  EXPECT_EQ(before[2], "code");
  ASSERT_GE(before_top.size(), 2u);
  EXPECT_EQ(before_top[0], "version");
  EXPECT_LT(std::find(before_top.begin(), before_top.end(), "symbols") -
                before_top.begin(),
            std::find(before_top.begin(), before_top.end(), "opend") -
                before_top.begin());

  mmtrade::config::SymbolConfig sym;
  sym.code = "HK.02208";
  sym.status = 2;
  sym.opened_qty = 150;
  sym.opened_price = 9.9;
  sym.initial_qty = 100.0;
  mmtrade::config::PersistStrategyConfigRuntime(main_path, sym);

  EXPECT_EQ(SymbolKeyOrder(main_path), before);
  EXPECT_EQ(TopLevelKeyOrder(main_path), before_top);
}

TEST(ConfigPersist, PersistStartupNotifyOnceOnly) {
  const std::string dir = TempDir();
  const std::string main_path = WriteJson(
      dir, "cfg.json",
      R"({
  "version": 1,
  "client_name": "x",
  "strategy_name": "t",
  "mode": "paper",
  "opend": {"host":"127.0.0.1","port":11121},
  "account": {"trd_market":"HK"},
  "symbols": [{"code":"HK.02208","entry_side":"BUY","entry_qty":100,"status":0,"startup_notify_once":true,"runtime_updated_at":"old"}]
})");

  mmtrade::config::PersistStartupNotifyOnceConsumed(main_path, "HK.02208");
  const nlohmann::json doc = nlohmann::json::parse(ReadFile(main_path));
  EXPECT_FALSE(doc.at("symbols").at(0).at("startup_notify_once").get<bool>());
  EXPECT_EQ(doc.at("symbols").at(0).at("status").get<int>(), 0);
  EXPECT_NE(doc.at("symbols").at(0).at("runtime_updated_at").get<std::string>(), "old");
}
