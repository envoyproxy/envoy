#include <filesystem>
#include <memory>

#include "source/extensions/common/aws/region_provider_impl.h"

#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

class RegionProviderChainTest : public testing::Test {
public:
  void SetUp() override {
    TestEnvironment::unsetEnvVar("HOME");
    TestEnvironment::unsetEnvVar("AWS_CONFIG");
    TestEnvironment::unsetEnvVar("AWS_PROFILE");
    TestEnvironment::unsetEnvVar("AWS_REGION");
    TestEnvironment::unsetEnvVar("AWS_DEFAULT_REGION");
    TestEnvironment::unsetEnvVar("AWS_SHARED_CREDENTIALS_FILE");
  }

  RegionProviderChain chain;
};

class EnvironmentRegionProviderTest : public testing::Test {
public:
  void SetUp() override {
    TestEnvironment::unsetEnvVar("HOME");
    TestEnvironment::unsetEnvVar("AWS_CONFIG");
    TestEnvironment::unsetEnvVar("AWS_PROFILE");
    TestEnvironment::unsetEnvVar("AWS_REGION");
    TestEnvironment::unsetEnvVar("AWS_DEFAULT_REGION");
    TestEnvironment::unsetEnvVar("AWS_SHARED_CREDENTIALS_FILE");
  }

  EnvironmentRegionProvider provider_;
};

class AWSCredentialsFileRegionProviderTest : public testing::Test {
public:
  void SetUp() override {
    TestEnvironment::unsetEnvVar("HOME");
    TestEnvironment::unsetEnvVar("AWS_CONFIG");
    TestEnvironment::unsetEnvVar("AWS_PROFILE");
    TestEnvironment::unsetEnvVar("AWS_REGION");
    TestEnvironment::unsetEnvVar("AWS_DEFAULT_REGION");
    TestEnvironment::unsetEnvVar("AWS_SHARED_CREDENTIALS_FILE");
  }

  AWSCredentialsFileRegionProvider provider_;
};

class AWSConfigFileRegionProviderTest : public testing::Test {
public:
  void SetUp() override {
    TestEnvironment::unsetEnvVar("HOME");
    TestEnvironment::unsetEnvVar("AWS_CONFIG");
    TestEnvironment::unsetEnvVar("AWS_PROFILE");
    TestEnvironment::unsetEnvVar("AWS_REGION");
    TestEnvironment::unsetEnvVar("AWS_DEFAULT_REGION");
    TestEnvironment::unsetEnvVar("AWS_SHARED_CREDENTIALS_FILE");
  }

  AWSConfigFileRegionProvider provider_;
};

TEST_F(EnvironmentRegionProviderTest, SomeRegion) {
  Envoy::Logger::Registry::setLogLevel(spdlog::level::debug);

  TestEnvironment::setEnvVar("AWS_REGION", "test-region", 1);
  EXPECT_EQ("test-region", provider_.getRegion().value());
}

TEST_F(EnvironmentRegionProviderTest, NoRegion) { EXPECT_FALSE(provider_.getRegion().has_value()); }

const char CREDENTIALS_FILE_CONTENTS[] =
    R"(
[default]
aws_access_key_id=default_access_key
aws_secret_access_key=default_secret
aws_session_token=default_token
region=credentialsdefaultregion

[profile1]
aws_access_key_id=profile1_acc=ess_key
aws_secret_access_key=profile1_secret
region=profile1region
)";

const char CONFIG_FILE_CONTENTS[] =
    R"(
[default]
region=configdefaultregion

[profile test]
region=testregion
)";
TEST_F(AWSConfigFileRegionProviderTest, CustomConfigFile) {
  auto temp = TestEnvironment::temporaryDirectory();
  TestEnvironment::setEnvVar("HOME", temp, 1);
  std::filesystem::create_directory(temp + "/.aws");
  std::string config_file(temp + "/.aws/customconfig");

  auto file_path =
      TestEnvironment::writeStringToFileForTest(config_file, CONFIG_FILE_CONTENTS, true, false);

  TestEnvironment::setEnvVar("AWS_CONFIG_FILE", config_file, 1);

  EXPECT_EQ("configdefaultregion", provider_.getRegion().value());
}

TEST_F(AWSConfigFileRegionProviderTest, CustomProfile) {
  auto temp = TestEnvironment::temporaryDirectory();
  TestEnvironment::setEnvVar("HOME", temp, 1);
  std::filesystem::create_directory(temp + "/.aws");
  std::string config_file(temp + "/.aws/config");

  auto file_path =
      TestEnvironment::writeStringToFileForTest(config_file, CONFIG_FILE_CONTENTS, true, false);

  TestEnvironment::setEnvVar("AWS_CONFIG_FILE", config_file, 1);
  TestEnvironment::setEnvVar("AWS_PROFILE", "test", 1);

  EXPECT_EQ("testregion", provider_.getRegion().value());
}

TEST_F(AWSCredentialsFileRegionProviderTest, CustomCredentialsFile) {
  auto temp = TestEnvironment::temporaryDirectory();
  TestEnvironment::setEnvVar("HOME", temp, 1);
  std::filesystem::create_directory(temp + "/.aws");
  std::string credentials_file(temp + "/.aws/customfile");

  auto file_path = TestEnvironment::writeStringToFileForTest(
      credentials_file, CREDENTIALS_FILE_CONTENTS, true, false);

  TestEnvironment::setEnvVar("AWS_SHARED_CREDENTIALS_FILE", credentials_file, 1);

  EXPECT_EQ("credentialsdefaultregion", provider_.getRegion().value());
}

TEST_F(AWSCredentialsFileRegionProviderTest, CustomProfile) {
  auto temp = TestEnvironment::temporaryDirectory();
  TestEnvironment::setEnvVar("HOME", temp, 1);
  std::filesystem::create_directory(temp + "/.aws");
  std::string credentials_file(temp + "/.aws/config");

  auto file_path = TestEnvironment::writeStringToFileForTest(
      credentials_file, CREDENTIALS_FILE_CONTENTS, true, false);

  TestEnvironment::setEnvVar("AWS_SHARED_CREDENTIALS_FILE", credentials_file, 1);
  TestEnvironment::setEnvVar("AWS_PROFILE", "profile1", 1);

  EXPECT_EQ("profile1region", provider_.getRegion().value());
}

TEST_F(RegionProviderChainTest, EnvironmentBeforeCredentialsFile) {
  auto temp = TestEnvironment::temporaryDirectory();
  TestEnvironment::setEnvVar("HOME", temp, 1);
  std::filesystem::create_directory(temp + "/.aws");
  std::string credentials_file(temp + "/.aws/credentials");

  auto file_path = TestEnvironment::writeStringToFileForTest(
      credentials_file, CREDENTIALS_FILE_CONTENTS, true, false);

  TestEnvironment::setEnvVar("AWS_REGION", "environmentregion", 1);
  EXPECT_EQ(chain.getRegion().value(), "environmentregion");
}

TEST_F(RegionProviderChainTest, EnvironmentBeforeConfigFile) {
  auto temp = TestEnvironment::temporaryDirectory();
  TestEnvironment::setEnvVar("HOME", temp, 1);
  std::filesystem::create_directory(temp + "/.aws");
  std::string config_file(temp + "/.aws/config");

  TestEnvironment::writeStringToFileForTest(config_file, CONFIG_FILE_CONTENTS, true, false);

  TestEnvironment::setEnvVar("AWS_REGION", "environmentregion", 1);
  EXPECT_EQ(chain.getRegion().value(), "environmentregion");
}

TEST_F(RegionProviderChainTest, CredentialsBeforeConfigFile) {
  auto temp = TestEnvironment::temporaryDirectory();
  TestEnvironment::setEnvVar("HOME", temp, 1);

  std::filesystem::create_directory(temp + "/.aws");
  std::string credentials_file(temp + "/.aws/credentials");

  TestEnvironment::writeStringToFileForTest(credentials_file, CREDENTIALS_FILE_CONTENTS, true,
                                            false);

  std::string config_file(temp + "/.aws/config");

  TestEnvironment::writeStringToFileForTest(config_file, CONFIG_FILE_CONTENTS, true, false);

  EXPECT_EQ(chain.getRegion().value(), "credentialsdefaultregion");
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
