#include "extensions/filters/http/common/aws/credentials_provider.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {
namespace Aws {

TEST(fromString, AllEmpty) {
  const auto c = Credentials::fromString(std::string(), std::string(), std::string());
  EXPECT_FALSE(c.accessKeyId().has_value());
  EXPECT_FALSE(c.secretAccessKey().has_value());
  EXPECT_FALSE(c.sessionToken().has_value());
}

TEST(fromString, OnlyAccessKeyId) {
  const auto c = Credentials::fromString("access_key", std::string(), std::string());
  EXPECT_EQ("access_key", c.accessKeyId());
  EXPECT_FALSE(c.secretAccessKey().has_value());
  EXPECT_FALSE(c.sessionToken().has_value());
}

TEST(fromString, AccessKeyIdAndSecretKey) {
  const auto c = Credentials::fromString("access_key", "secret_key", std::string());
  EXPECT_EQ("access_key", c.accessKeyId());
  EXPECT_EQ("secret_key", c.secretAccessKey());
  EXPECT_FALSE(c.sessionToken().has_value());
}

TEST(fromString, AllNonEmpty) {
  const auto c = Credentials::fromString("access_key", "secret_key", "session_token");
  EXPECT_EQ("access_key", c.accessKeyId());
  EXPECT_EQ("secret_key", c.secretAccessKey());
  EXPECT_EQ("session_token", c.sessionToken());
}

TEST(fromCString, AllNull) {
  const auto c = Credentials::fromCString(nullptr, nullptr, nullptr);
  EXPECT_FALSE(c.accessKeyId().has_value());
  EXPECT_FALSE(c.secretAccessKey().has_value());
  EXPECT_FALSE(c.sessionToken().has_value());
}

TEST(fromCString, AllEmpty) {
  const auto c = Credentials::fromCString("", "", "");
  EXPECT_FALSE(c.accessKeyId().has_value());
  EXPECT_FALSE(c.secretAccessKey().has_value());
  EXPECT_FALSE(c.sessionToken().has_value());
}

TEST(fromCString, OnlyAccessKeyId) {
  const auto c = Credentials::fromCString("access_key", "", "");
  EXPECT_EQ("access_key", c.accessKeyId());
  EXPECT_FALSE(c.secretAccessKey().has_value());
  EXPECT_FALSE(c.sessionToken().has_value());
}

TEST(fromCString, AccessKeyIdAndSecretKey) {
  const auto c = Credentials::fromCString("access_key", "secret_key", "");
  EXPECT_EQ("access_key", c.accessKeyId());
  EXPECT_EQ("secret_key", c.secretAccessKey());
  EXPECT_FALSE(c.sessionToken().has_value());
}

TEST(fromCString, AllNonEmpty) {
  const auto c = Credentials::fromCString("access_key", "secret_key", "session_token");
  EXPECT_EQ("access_key", c.accessKeyId());
  EXPECT_EQ("secret_key", c.secretAccessKey());
  EXPECT_EQ("session_token", c.sessionToken());
}

} // namespace Aws
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
