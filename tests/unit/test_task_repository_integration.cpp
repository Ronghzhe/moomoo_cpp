#include "db/mysql_client.hpp"
#include "db/mysql_config.hpp"
#include "task/task_repository.hpp"
#include "task/task_types.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <ctime>
#include <sstream>
#include <string>

namespace {

bool IntegrationEnabled() {
  const char *env = std::getenv("MOOMOO_MYSQL_INTEGRATION");
  return env != NULL && env[0] == '1';
}

std::string MysqlConfigPath() {
  const char *env = std::getenv("MOOMOO_MYSQL_CONFIG");
  if (env != NULL && env[0] != '\0') {
    return std::string(env);
  }
  return "/home/ubuntu/workspace/mysql/mysql_config.json";
}

}  // namespace

TEST(TaskRepositoryIntegration, RoundTripPendingToReady) {
  if (!IntegrationEnabled()) {
    GTEST_SKIP() << "set MOOMOO_MYSQL_INTEGRATION=1 to run";
  }

  mmtrade::db::MysqlConfig cfg = mmtrade::db::LoadMysqlConfig(MysqlConfigPath());
  cfg.database = mmtrade::db::DefaultStrategyTradeDatabase();

  mmtrade::db::MysqlClient db(cfg);
  ASSERT_TRUE(db.Connect()) << db.LastError();

  mmtrade::task::TaskRepository repo(&db);

  const std::string task_name = "cpp_f1_probe_" + std::to_string(static_cast<long long>(time(NULL)));
  const std::string params = "{\"task_type\":\"entry_now\",\"symbol\":\"US.AAPL\",\"task_time\":1}";

  std::ostringstream ins;
  ins << "INSERT INTO strategy_tasks "
         "(f_uuid, f_task_type, f_task_name, f_user, f_trader, f_task_params, f_status) VALUES (0, "
      << mmtrade::task::kCppTaskType << ", '" << db.Escape(task_name) << "', 'cpp_test', 'cpp_test', '"
      << db.Escape(params) << "', " << mmtrade::task::kStatusPending << ")";
  ASSERT_TRUE(db.Execute(ins.str())) << db.LastError();

  mmtrade::db::MysqlRows id_rows;
  ASSERT_TRUE(db.Query("SELECT f_id FROM strategy_tasks WHERE f_task_name = '" + db.Escape(task_name) + "' LIMIT 1",
                       &id_rows));
  ASSERT_EQ(id_rows.size(), 1u);
  const int64_t f_id = atoll(id_rows[0]["f_id"].c_str());
  ASSERT_GT(f_id, 0);

  mmtrade::task::PrepareClaim claim = repo.TryAcquirePrepareSlot("cpp_itest_worker", mmtrade::task::kCppTaskType);
  ASSERT_TRUE(claim.acquired);
  EXPECT_EQ(claim.task.f_id, f_id);
  EXPECT_EQ(claim.task.f_task_name, task_name);

  const std::string result_json = "{\"ok\":true,\"note\":\"cpp f1 integration\"}";
  EXPECT_TRUE(repo.FinalizePrepare(claim.task, true, result_json, params));
  repo.ReleasePrepareLock(claim.lock_key);

  int status = -1;
  ASSERT_TRUE(repo.FetchTaskStatus(f_id, &status));
  EXPECT_EQ(status, mmtrade::task::kStatusReady);

  std::ostringstream cleanup;
  cleanup << "DELETE FROM strategy_tasks WHERE f_id = " << f_id;
  ASSERT_TRUE(db.Execute(cleanup.str()));
}
