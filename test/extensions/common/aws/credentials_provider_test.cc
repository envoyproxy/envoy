#include "source/extensions/common/aws/credentials_provider.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

TEST(Credentials, Default) {
  const auto c = Credentials();
  EXPECT_FALSE(c.accessKeyId().has_value());
  EXPECT_FALSE(c.secretAccessKey().has_value());
  EXPECT_FALSE(c.sessionToken().has_value());
}

TEST(Credentials, AllNull) {
  const auto c = Credentials({}, {}, {});
  EXPECT_FALSE(c.accessKeyId().has_value());
  EXPECT_FALSE(c.secretAccessKey().has_value());
  EXPECT_FALSE(c.sessionToken().has_value());
}

TEST(Credentials, AllEmpty) {
  const auto c = Credentials("", "", "");
  EXPECT_FALSE(c.accessKeyId().has_value());
  EXPECT_FALSE(c.secretAccessKey().has_value());
  EXPECT_FALSE(c.sessionToken().has_value());
}

TEST(Credentials, OnlyAccessKeyId) {
  const auto c = Credentials("access_key", "", "");
  EXPECT_EQ("access_key", c.accessKeyId());
  EXPECT_FALSE(c.secretAccessKey().has_value());
  EXPECT_FALSE(c.sessionToken().has_value());
}

TEST(Credentials, AccessKeyIdAndSecretKey) {
  const auto c = Credentials("access_key", "secret_key", "");
  EXPECT_EQ("access_key", c.accessKeyId());
  EXPECT_EQ("secret_key", c.secretAccessKey());
  EXPECT_FALSE(c.sessionToken().has_value());
}

TEST(Credentials, AllNonEmpty) {
  const auto c = Credentials("access_key", "secret_key", "session_token");
  EXPECT_EQ("access_key", c.accessKeyId());
  EXPECT_EQ("secret_key", c.secretAccessKey());
  EXPECT_EQ("session_token", c.sessionToken());
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
