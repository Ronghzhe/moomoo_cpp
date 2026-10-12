#include "db/mysql_config.hpp"
#include "task/task_types.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

namespace {

std::string FixturesDir() { return std::string(MOOMOO_FIXTURES_DIR); }

}  // namespace

TEST(MysqlConfigTest, LoadSampleFixture) {
  const std::string path = FixturesDir() + "/mysql_config_sample.json";
  const mmtrade::db::MysqlConfig cfg = mmtrade::db::LoadMysqlConfig(path);
  EXPECT_EQ(cfg.host, "127.0.0.1");
  EXPECT_EQ(cfg.port, 3306);
  EXPECT_EQ(cfg.user, "test_user");
  EXPECT_EQ(cfg.password, "test_pass");
  EXPECT_EQ(cfg.charset, "utf8mb4");
  EXPECT_EQ(cfg.database, mmtrade::db::DefaultStrategyTradeDatabase());
}

TEST(TaskTypesTest, PrepareLockKeyMatchesPython) {
  EXPECT_EQ(mmtrade::task::PrepareLockKey(42), "stp_42");
  EXPECT_EQ(mmtrade::task::kCppTaskType, 2);
  EXPECT_EQ(mmtrade::task::kStatusPending, 1);
  EXPECT_EQ(mmtrade::task::kStatusReady, 2);
  EXPECT_EQ(mmtrade::task::kStatusDone, 3);
}

TEST(TaskTypesTest, TaskRowFromMapParsesFields) {
  std::map<std::string, std::string> row;
  row["f_id"] = "99";
  row["f_task_name"] = "demo_task";
  row["f_task_type"] = "2";
  row["f_user"] = "nana";
  row["f_trader"] = "nana";
  row["f_task_params"] = "{\"symbol\":\"US.AAPL\"}";
  row["f_updatetime"] = "2026-08-13 10:00:00";
  row["f_status"] = "1";

  const mmtrade::task::TaskRow task = mmtrade::task::TaskRowFromMap(row);
  EXPECT_EQ(task.f_id, 99);
  EXPECT_EQ(task.f_task_name, "demo_task");
  EXPECT_EQ(task.f_task_type, 2);
  EXPECT_EQ(task.f_user, "nana");
  EXPECT_EQ(task.f_trader, "nana");
  EXPECT_EQ(task.f_task_params, "{\"symbol\":\"US.AAPL\"}");
  EXPECT_EQ(task.f_status, 1);
}
