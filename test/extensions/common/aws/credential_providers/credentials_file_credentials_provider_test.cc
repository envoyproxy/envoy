#include "source/extensions/common/aws/credential_providers/credentials_file_credentials_provider.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

using testing::InSequence;
using testing::InvokeWithoutArgs;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

const char CREDENTIALS_FILE[] = "test-credentials.json";
const char CREDENTIALS_FILE_CONTENTS[] =
    R"(
  [default]
  aws_access_key_id=default_access_key
  aws_secret_access_key=default_secret
  aws_session_token=default_token

  # This profile has leading spaces that should get trimmed.
    [profile1]
  # The "=" in the value should not interfere with how this line is parsed.
  aws_access_key_id=profile1_acc=ess_key
  aws_secret_access_key=profile1_secret
  foo=bar
  aws_session_token=profile1_token

  [profile2]
  aws_access_key_id=profile2_access_key

  [profile3]
  aws_access_key_id=profile3_access_key
  aws_secret_access_key=

  [profile4]
  aws_access_key_id = profile4_access_key
  aws_secret_access_key = profile4_secret
  aws_session_token = profile4_token
  )";

class CredentialsFileCredentialsProviderTest : public testing::Test {
public:
  CredentialsFileCredentialsProviderTest()
      : api_(Api::createApiForTest(time_system_)), provider_(context_) {}

  ~CredentialsFileCredentialsProviderTest() override {
    TestEnvironment::unsetEnvVar("AWS_SHARED_CREDENTIALS_FILE");
    TestEnvironment::unsetEnvVar("AWS_PROFILE");
  }

  void SetUp() override { EXPECT_CALL(context_, api()).WillRepeatedly(testing::ReturnRef(*api_)); }

  void setUpTest(std::string file_contents, std::string profile) {
    auto file_path = TestEnvironment::writeStringToFileForTest(CREDENTIALS_FILE, file_contents);
    TestEnvironment::setEnvVar("AWS_SHARED_CREDENTIALS_FILE", file_path, 1);
    TestEnvironment::setEnvVar("AWS_PROFILE", profile, 1);
  }

  Event::SimulatedTimeSystem time_system_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Event::MockDispatcher dispatcher_;

  Api::ApiPtr api_;
  CredentialsFileCredentialsProvider provider_;
};

TEST_F(CredentialsFileCredentialsProviderTest, CustomProfileFromConfigShouldBeHonored) {
  auto file_path =
      TestEnvironment::writeStringToFileForTest(CREDENTIALS_FILE, CREDENTIALS_FILE_CONTENTS);
  TestEnvironment::setEnvVar("AWS_SHARED_CREDENTIALS_FILE", file_path, 1);
  envoy::extensions::common::aws::v3::CredentialsFileCredentialProvider config = {};
  config.set_profile("profile4");
  auto provider = CredentialsFileCredentialsProvider(context_, config);
  const auto credentials = provider.getCredentials();
  EXPECT_EQ("profile4_access_key", credentials.accessKeyId().value());
  EXPECT_EQ("profile4_secret", credentials.secretAccessKey().value());
  EXPECT_EQ("profile4_token", credentials.sessionToken().value());
}

TEST_F(CredentialsFileCredentialsProviderTest, CustomProfileFromConfigWithWatched) {
  auto file_path =
      TestEnvironment::writeStringToFileForTest(CREDENTIALS_FILE, CREDENTIALS_FILE_CONTENTS);

  envoy::extensions::common::aws::v3::CredentialsFileCredentialProvider config = {};
  config.mutable_credentials_data_source()->set_filename(file_path);
  config.mutable_credentials_data_source()->mutable_watched_directory()->set_path(
      TestEnvironment::temporaryPath("test"));
  EXPECT_CALL(context_, api()).WillRepeatedly(ReturnRef(*api_));
  EXPECT_CALL(context_, mainThreadDispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
  EXPECT_CALL(dispatcher_, isThreadSafe()).WillRepeatedly(Return(true));
  EXPECT_CALL(dispatcher_, createFilesystemWatcher_()).WillRepeatedly(InvokeWithoutArgs([&] {
    Filesystem::MockWatcher* mock_watcher = new NiceMock<Filesystem::MockWatcher>();
    EXPECT_CALL(*mock_watcher, addWatch(_, Filesystem::Watcher::Events::MovedTo, _))
        .WillRepeatedly(Return(absl::OkStatus()));
    return mock_watcher;
  }));

  auto provider = CredentialsFileCredentialsProvider(context_, config);
  const auto credentials = provider.getCredentials();
  EXPECT_EQ("default_access_key", credentials.accessKeyId().value());
  EXPECT_EQ("default_secret", credentials.secretAccessKey().value());
  EXPECT_EQ("default_token", credentials.sessionToken().value());
}

TEST_F(CredentialsFileCredentialsProviderTest, CustomFilePathFromConfig) {
  auto file_path =
      TestEnvironment::writeStringToFileForTest(CREDENTIALS_FILE, CREDENTIALS_FILE_CONTENTS);

  envoy::extensions::common::aws::v3::CredentialsFileCredentialProvider config = {};
  config.mutable_credentials_data_source()->set_filename(file_path);
  auto provider = CredentialsFileCredentialsProvider(context_, config);
  const auto credentials = provider.getCredentials();
  EXPECT_EQ("default_access_key", credentials.accessKeyId().value());
  EXPECT_EQ("default_secret", credentials.secretAccessKey().value());
  EXPECT_EQ("default_token", credentials.sessionToken().value());
}

TEST_F(CredentialsFileCredentialsProviderTest, CustomFilePathAndProfileFromConfig) {
  auto file_path =
      TestEnvironment::writeStringToFileForTest(CREDENTIALS_FILE, CREDENTIALS_FILE_CONTENTS);

  envoy::extensions::common::aws::v3::CredentialsFileCredentialProvider config = {};
  config.mutable_credentials_data_source()->set_filename(file_path);
  config.set_profile("profile4");

  auto provider = CredentialsFileCredentialsProvider(context_, config);
  const auto credentials = provider.getCredentials();
  EXPECT_EQ("profile4_access_key", credentials.accessKeyId().value());
  EXPECT_EQ("profile4_secret", credentials.secretAccessKey().value());
  EXPECT_EQ("profile4_token", credentials.sessionToken().value());
}

TEST_F(CredentialsFileCredentialsProviderTest, UnexistingCustomProfileFomConfig) {
  auto file_path =
      TestEnvironment::writeStringToFileForTest(CREDENTIALS_FILE, CREDENTIALS_FILE_CONTENTS);
  TestEnvironment::setEnvVar("AWS_SHARED_CREDENTIALS_FILE", file_path, 1);

  envoy::extensions::common::aws::v3::CredentialsFileCredentialProvider config = {};
  config.set_profile("unexistening_profile");

  auto provider = CredentialsFileCredentialsProvider(context_, config);
  const auto credentials = provider.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(CredentialsFileCredentialsProviderTest, FileDoesNotExist) {
  TestEnvironment::setEnvVar("AWS_SHARED_CREDENTIALS_FILE", "/file/does/not/exist", 1);
  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(CredentialsFileCredentialsProviderTest, DefaultCredentialsFile) {
  TestEnvironment::unsetEnvVar("AWS_SHARED_CREDENTIALS_FILE");
  auto temp = TestEnvironment::temporaryDirectory();
  std::filesystem::create_directory(temp + "/.aws");
  std::string credential_file(temp + "/.aws/credentials");

  auto file_path = TestEnvironment::writeStringToFileForTest(
      credential_file, CREDENTIALS_FILE_CONTENTS, true, false);

  TestEnvironment::setEnvVar("HOME", temp, 1);
  TestEnvironment::setEnvVar("AWS_PROFILE", "profile1", 1);

  const auto credentials = provider_.getCredentials();
  EXPECT_EQ("profile1_acc=ess_key", credentials.accessKeyId().value());
  EXPECT_EQ("profile1_secret", credentials.secretAccessKey().value());
  EXPECT_EQ("profile1_token", credentials.sessionToken().value());
}

TEST_F(CredentialsFileCredentialsProviderTest, ProfileDoesNotExist) {
  setUpTest(CREDENTIALS_FILE_CONTENTS, "invalid_profile");

  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(CredentialsFileCredentialsProviderTest, IncompleteProfile) {
  setUpTest(CREDENTIALS_FILE_CONTENTS, "profile2");

  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(CredentialsFileCredentialsProviderTest, DefaultProfile) {
  setUpTest(CREDENTIALS_FILE_CONTENTS, "");

  const auto credentials = provider_.getCredentials();
  EXPECT_EQ("default_access_key", credentials.accessKeyId().value());
  EXPECT_EQ("default_secret", credentials.secretAccessKey().value());
  EXPECT_EQ("default_token", credentials.sessionToken().value());
}

TEST_F(CredentialsFileCredentialsProviderTest, CompleteProfile) {
  setUpTest(CREDENTIALS_FILE_CONTENTS, "profile1");

  const auto credentials = provider_.getCredentials();
  EXPECT_EQ("profile1_acc=ess_key", credentials.accessKeyId().value());
  EXPECT_EQ("profile1_secret", credentials.secretAccessKey().value());
  EXPECT_EQ("profile1_token", credentials.sessionToken().value());
}

TEST_F(CredentialsFileCredentialsProviderTest, EmptySecret) {
  setUpTest(CREDENTIALS_FILE_CONTENTS, "profile3");

  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(CredentialsFileCredentialsProviderTest, SpacesBetweenParts) {
  setUpTest(CREDENTIALS_FILE_CONTENTS, "profile4");

  const auto credentials = provider_.getCredentials();
  EXPECT_EQ("profile4_access_key", credentials.accessKeyId().value());
  EXPECT_EQ("profile4_secret", credentials.secretAccessKey().value());
  EXPECT_EQ("profile4_token", credentials.sessionToken().value());
}

TEST_F(CredentialsFileCredentialsProviderTest, RefreshInterval) {
  InSequence sequence;
  TestEnvironment::setEnvVar("AWS_SHARED_CREDENTIALS_FILE", "/file/does/not/exist", 1);

  auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());

  // Credentials won't be extracted even after we switch to a legitimate profile
  // with valid credentials.
  setUpTest(CREDENTIALS_FILE_CONTENTS, "profile1");
  credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());

  // Credentials will be extracted again after the REFRESH_INTERVAL.
  time_system_.advanceTimeWait(std::chrono::hours(2));
  credentials = provider_.getCredentials();
  EXPECT_EQ("profile1_acc=ess_key", credentials.accessKeyId().value());
  EXPECT_EQ("profile1_secret", credentials.secretAccessKey().value());
  EXPECT_EQ("profile1_token", credentials.sessionToken().value());

  // Previously cached credentials will be used.
  setUpTest(CREDENTIALS_FILE_CONTENTS, "default");
  credentials = provider_.getCredentials();
  EXPECT_EQ("profile1_acc=ess_key", credentials.accessKeyId().value());
  EXPECT_EQ("profile1_secret", credentials.secretAccessKey().value());
  EXPECT_EQ("profile1_token", credentials.sessionToken().value());

  // Credentials will be extracted again after the REFRESH_INTERVAL.
  time_system_.advanceTimeWait(std::chrono::hours(2));
  credentials = provider_.getCredentials();
  EXPECT_EQ("default_access_key", credentials.accessKeyId().value());
  EXPECT_EQ("default_secret", credentials.secretAccessKey().value());
  EXPECT_EQ("default_token", credentials.sessionToken().value());
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
