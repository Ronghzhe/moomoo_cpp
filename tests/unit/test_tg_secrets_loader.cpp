#include "notify/tg_secrets_loader.hpp"

#include <gtest/gtest.h>

#include <string>

TEST(TgSecretsLoaderTest, ParseSampleFixture) {
  const std::string path = std::string(MOOMOO_FIXTURES_DIR) + "/tg_bot_sample.txt";
  mmtrade::notify::TgBotSecrets secrets;
  std::string err;
  ASSERT_TRUE(mmtrade::notify::LoadTgBotSecrets(path, &secrets, &err)) << err;
  EXPECT_EQ(secrets.bot_token, "123456789:ABCdefGHIjklMNOpqrSTUvwxYZ");
  EXPECT_EQ(secrets.paper_chat_id, "-5560998729");
  EXPECT_EQ(secrets.paper_chat_title, "quant-stock模拟盘测试群");
}

TEST(TgSecretsLoaderTest, MaskBotToken) {
  EXPECT_EQ(mmtrade::notify::MaskBotToken("123456789:ABC"), "123456***");
  EXPECT_EQ(mmtrade::notify::MaskBotToken(""), "(empty)");
}
