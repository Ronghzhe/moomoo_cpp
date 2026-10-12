#include "log/strategy_store.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

std::string TempDir() {
  char tmpl[] = "/tmp/moomoo_store_test_XXXXXX";
  char *d = mkdtemp(tmpl);
  return d != NULL ? std::string(d) : std::string(".");
}

bool FileExists(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

}  // namespace

TEST(StrategyStoreTest, SaveAndUpdateOrder) {
  const std::string dir = TempDir();
  mmtrade::log::StrategyStore store(dir, "US.ASTS", 12345, 0);
  EXPECT_TRUE(FileExists(store.csv_dir_path() + "/events.csv"));
  EXPECT_TRUE(FileExists(store.csv_dir_path() + "/orders.csv"));
  store.LogEvent("INFO", "startup", "ok");
  EXPECT_TRUE(FileExists(store.runtime_log_path()));
  store.SaveOrder("task1", "US.ASTS", "BUY", "1001", "SUBMITTED", 10.0, 5.5, "test", "", "",
                  "", "real");
  EXPECT_TRUE(store.UpdateOrderFill("1001", "FILLED_ALL", 5.6, 10.0));

  std::ifstream orders((store.csv_dir_path() + "/orders.csv").c_str());
  std::string content((std::istreambuf_iterator<char>(orders)), std::istreambuf_iterator<char>());
  EXPECT_NE(content.find("1001"), std::string::npos);
  EXPECT_NE(content.find("FILLED_ALL"), std::string::npos);
  EXPECT_NE(content.find("5.6"), std::string::npos);
}
