#include "config/config_path_resolver.hpp"

#include <gtest/gtest.h>

namespace {

char *Arg(const char *s) {
  return const_cast<char *>(s);
}

}  // namespace

TEST(ConfigPathResolver, ConfigFlagWins) {
  char *argv[] = {Arg("strategy_cpp"), Arg("-c"), Arg("/tmp/foo.json"), Arg("--poll-sec"),
                  Arg("5")};
  const mmtrade::config::ConfigPathResolveResult out =
      mmtrade::config::ResolveStrategyConfigPath(5, argv);
  ASSERT_TRUE(out.ok);
  EXPECT_EQ(out.path, "/tmp/foo.json");
}

TEST(ConfigPathResolver, PositionalConfig) {
  char *argv[] = {Arg("strategy_cpp"), Arg("relative/config.json")};
  const mmtrade::config::ConfigPathResolveResult out =
      mmtrade::config::ResolveStrategyConfigPath(2, argv);
  ASSERT_TRUE(out.ok);
  EXPECT_NE(out.path.find("relative/config.json"), std::string::npos);
}

TEST(ConfigPathResolver, MissingConfigFails) {
  char *argv[] = {Arg("strategy_cpp"), Arg("--poll-sec"), Arg("5")};
  const mmtrade::config::ConfigPathResolveResult out =
      mmtrade::config::ResolveStrategyConfigPath(3, argv);
  EXPECT_FALSE(out.ok);
  EXPECT_FALSE(out.error.empty());
}
