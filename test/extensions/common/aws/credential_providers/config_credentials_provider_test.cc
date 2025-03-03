#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "source/extensions/common/aws/credential_providers/config_credentials_provider.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

class ConfigCredentialsProviderTest : public testing::Test {
public:
  ~ConfigCredentialsProviderTest() override = default;
};

TEST_F(ConfigCredentialsProviderTest, ConfigShouldBeHonored) {
  auto provider = ConfigCredentialsProvider("akid", "secret", "session_token");
  const auto credentials = provider.getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("session_token", credentials.sessionToken().value());
}

TEST_F(ConfigCredentialsProviderTest, SessionTokenIsOptional) {
  auto provider = ConfigCredentialsProvider("akid", "secret", "");
  const auto credentials = provider.getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(ConfigCredentialsProviderTest, AssessKeyIdIsRequired) {
  auto provider = ConfigCredentialsProvider("", "secret", "");
  const auto credentials = provider.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
