#include "common/aws/credentials_provider_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/aws/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"

using testing::_;
using testing::InSequence;
using testing::NiceMock;
using testing::Ref;
using testing::Return;

namespace Envoy {
namespace Aws {
namespace Auth {

class EvironmentCredentialsProviderTest : public testing::Test {
public:
  ~EvironmentCredentialsProviderTest() {
    TestEnvironment::unsetEnvVar("AWS_ACCESS_KEY_ID");
    TestEnvironment::unsetEnvVar("AWS_SECRET_ACCESS_KEY");
    TestEnvironment::unsetEnvVar("AWS_SESSION_TOKEN");
  }

  EnvironmentCredentialsProvider provider_;
};

TEST_F(EvironmentCredentialsProviderTest, AllEnvironmentVars) {
  TestEnvironment::setEnvVar("AWS_ACCESS_KEY_ID", "akid", 1);
  TestEnvironment::setEnvVar("AWS_SECRET_ACCESS_KEY", "secret", 1);
  TestEnvironment::setEnvVar("AWS_SESSION_TOKEN", "token", 1);
  const auto credentials = provider_.getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());
}

TEST_F(EvironmentCredentialsProviderTest, NoEnvironmentVars) {
  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(EvironmentCredentialsProviderTest, MissingAccessKeyId) {
  TestEnvironment::setEnvVar("AWS_SECRET_ACCESS_KEY", "secret", 1);
  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(EvironmentCredentialsProviderTest, NoSessionToken) {
  TestEnvironment::setEnvVar("AWS_ACCESS_KEY_ID", "akid", 1);
  TestEnvironment::setEnvVar("AWS_SECRET_ACCESS_KEY", "secret", 1);
  const auto credentials = provider_.getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

class InstanceProfileCredentialsProviderTest : public testing::Test {
public:
  InstanceProfileCredentialsProviderTest()
      : fetcher_(new NiceMock<MockMetadataFetcher>()),
        provider_(api_, time_system_, MetadataFetcherPtr{fetcher_}) {}

  void expectCredentialListing(const absl::optional<std::string>& listing) {
    EXPECT_CALL(api_, allocateDispatcher_(Ref(time_system_)));
    EXPECT_CALL(*fetcher_, getMetadata(_, "169.254.169.254:80",
                                       "/latest/meta-data/iam/security-credentials", _))
        .WillOnce(Return(listing));
  }

  void expectDocument(const absl::optional<std::string>& document) {
    EXPECT_CALL(*fetcher_, getMetadata(_, "169.254.169.254:80",
                                       "/latest/meta-data/iam/security-credentials/doc1", _))
        .WillOnce(Return(document));
  }

  NiceMock<Api::MockApi> api_;
  NiceMock<Event::MockDispatcher>* dispatcher_;
  Event::SimulatedTimeSystem time_system_;
  NiceMock<MockMetadataFetcher>* fetcher_;
  InstanceProfileCredentialsProvider provider_;
};

TEST_F(InstanceProfileCredentialsProviderTest, FailedCredentailListing) {
  expectCredentialListing(absl::optional<std::string>());
  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderTest, EmptyCredentialListing) {
  expectCredentialListing("");
  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderTest, MissingDocument) {
  expectCredentialListing("doc1\ndoc2\ndoc3");
  expectDocument(absl::optional<std::string>());
  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderTest, MalformedDocumenet) {
  expectCredentialListing("doc1");
  expectDocument(R"EOF(
not json
)EOF");
  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderTest, EmptyValues) {
  expectCredentialListing("doc1");
  expectDocument(R"EOF(
{
  "AccessKeyId": "",
  "SecretAccessKey": "",
  "Token": ""
}
)EOF");
  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(InstanceProfileCredentialsProviderTest, FullCachedCredentials) {
  expectCredentialListing("doc1");
  expectDocument(R"EOF(
{
  "AccessKeyId": "akid",
  "SecretAccessKey": "secret",
  "Token": "token"
}
)EOF");
  const auto credentials = provider_.getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());
  const auto cached_credentials = provider_.getCredentials();
  EXPECT_EQ("akid", cached_credentials.accessKeyId().value());
  EXPECT_EQ("secret", cached_credentials.secretAccessKey().value());
  EXPECT_EQ("token", cached_credentials.sessionToken().value());
}

TEST_F(InstanceProfileCredentialsProviderTest, CredentialExpiration) {
  InSequence sequence;
  expectCredentialListing("doc1");
  expectDocument(R"EOF(
{
  "AccessKeyId": "akid",
  "SecretAccessKey": "secret",
  "Token": "token"
}
)EOF");
  const auto credentials = provider_.getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());
  time_system_.sleep(std::chrono::hours(2));
  expectCredentialListing("doc1");
  expectDocument(R"EOF(
{
  "AccessKeyId": "new_akid",
  "SecretAccessKey": "new_secret",
  "Token": "new_token"
}
)EOF");
  const auto new_credentials = provider_.getCredentials();
  EXPECT_EQ("new_akid", new_credentials.accessKeyId().value());
  EXPECT_EQ("new_secret", new_credentials.secretAccessKey().value());
  EXPECT_EQ("new_token", new_credentials.sessionToken().value());
}

class TaskRoleCredentialsProviderTest : public testing::Test {
public:
  TaskRoleCredentialsProviderTest()
      : fetcher_(new NiceMock<MockMetadataFetcher>()),
        provider_(api_, time_system_, MetadataFetcherPtr{fetcher_}, "169.254.170.2:80/path/to/doc",
                  "auth_token") {
    // 20180102T030405Z
    time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));
  }

  void expectDocument(const absl::optional<std::string>& document) {
    EXPECT_CALL(api_, allocateDispatcher_(Ref(time_system_)));
    EXPECT_CALL(*fetcher_, getMetadata(_, "169.254.170.2:80", "/path/to/doc", _))
        .WillOnce(Return(document));
  }

  NiceMock<Api::MockApi> api_;
  Event::SimulatedTimeSystem time_system_;
  NiceMock<MockMetadataFetcher>* fetcher_;
  TaskRoleCredentialsProvider provider_;
};

TEST_F(TaskRoleCredentialsProviderTest, FailedFetchingDocument) {
  expectDocument(absl::optional<std::string>());
  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(TaskRoleCredentialsProviderTest, MalformedDocumenet) {
  expectDocument(R"EOF(
not json
)EOF");
  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(TaskRoleCredentialsProviderTest, EmptyValues) {
  expectDocument(R"EOF(
{
  "AccessKeyId": "",
  "SecretAccessKey": "",
  "Token": "",
  "Expiration": ""
}
)EOF");
  const auto credentials = provider_.getCredentials();
  EXPECT_FALSE(credentials.accessKeyId().has_value());
  EXPECT_FALSE(credentials.secretAccessKey().has_value());
  EXPECT_FALSE(credentials.sessionToken().has_value());
}

TEST_F(TaskRoleCredentialsProviderTest, FullCachedCredentials) {
  expectDocument(R"EOF(
{
  "AccessKeyId": "akid",
  "SecretAccessKey": "secret",
  "Token": "token",
  "Expiration": "20180102T030500Z"
}
)EOF");
  const auto credentials = provider_.getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());
  const auto cached_credentials = provider_.getCredentials();
  EXPECT_EQ("akid", cached_credentials.accessKeyId().value());
  EXPECT_EQ("secret", cached_credentials.secretAccessKey().value());
  EXPECT_EQ("token", cached_credentials.sessionToken().value());
}

TEST_F(TaskRoleCredentialsProviderTest, NormalCredentialExpiration) {
  InSequence sequence;
  expectDocument(R"EOF(
{
  "AccessKeyId": "akid",
  "SecretAccessKey": "secret",
  "Token": "token",
  "Expiration": "20190102T030405Z"
}
)EOF");
  const auto credentials = provider_.getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());
  time_system_.sleep(std::chrono::hours(2));
  expectDocument(R"EOF(
{
  "AccessKeyId": "new_akid",
  "SecretAccessKey": "new_secret",
  "Token": "new_token",
  "Expiration": "20190102T030405Z"
}
)EOF");
  const auto cached_credentials = provider_.getCredentials();
  EXPECT_EQ("new_akid", cached_credentials.accessKeyId().value());
  EXPECT_EQ("new_secret", cached_credentials.secretAccessKey().value());
  EXPECT_EQ("new_token", cached_credentials.sessionToken().value());
}

TEST_F(TaskRoleCredentialsProviderTest, TimestampCredentialExpiration) {
  InSequence sequence;
  expectDocument(R"EOF(
{
  "AccessKeyId": "akid",
  "SecretAccessKey": "secret",
  "Token": "token",
  "Expiration": "20180102T030405Z"
}
)EOF");
  const auto credentials = provider_.getCredentials();
  EXPECT_EQ("akid", credentials.accessKeyId().value());
  EXPECT_EQ("secret", credentials.secretAccessKey().value());
  EXPECT_EQ("token", credentials.sessionToken().value());
  expectDocument(R"EOF(
{
  "AccessKeyId": "new_akid",
  "SecretAccessKey": "new_secret",
  "Token": "new_token",
  "Expiration": "20190102T030405Z"
}
)EOF");
  const auto cached_credentials = provider_.getCredentials();
  EXPECT_EQ("new_akid", cached_credentials.accessKeyId().value());
  EXPECT_EQ("new_secret", cached_credentials.secretAccessKey().value());
  EXPECT_EQ("new_token", cached_credentials.sessionToken().value());
}

class DefaultCredentialsProviderChainTest : public testing::Test {
public:
  DefaultCredentialsProviderChainTest() {
    EXPECT_CALL(factories_, createEnvironmentCredentialsProvider());
  }

  ~DefaultCredentialsProviderChainTest() {
    TestEnvironment::unsetEnvVar("AWS_CONTAINER_CREDENTIALS_RELATIVE_URI");
    TestEnvironment::unsetEnvVar("AWS_CONTAINER_CREDENTIALS_FULL_URI");
    TestEnvironment::unsetEnvVar("AWS_CONTAINER_AUTHORIZATION_TOKEN");
    TestEnvironment::unsetEnvVar("AWS_EC2_METADATA_DISABLED");
  }

  class MockCredentialsProviderChainFactories : public CredentialsProviderChainFactories {
  public:
    MOCK_CONST_METHOD0(createMetadataFetcher, MetadataFetcherPtr());
    MOCK_CONST_METHOD0(createEnvironmentCredentialsProvider, CredentialsProviderSharedPtr());
    MOCK_CONST_METHOD5(createTaskRoleCredentialsProvider,
                       CredentialsProviderSharedPtr(Api::Api&, Event::TimeSystem&,
                                                    MetadataFetcherPtr&&, const std::string&,
                                                    const absl::optional<std::string>&));
    MOCK_CONST_METHOD3(createInstanceProfileCredentialsProvider,
                       CredentialsProviderSharedPtr(Api::Api&, Event::TimeSystem&,
                                                    MetadataFetcherPtr&& fetcher));
  };

  NiceMock<Api::MockApi> api_;
  Event::SimulatedTimeSystem time_system_;
  NiceMock<MockCredentialsProviderChainFactories> factories_;
};

TEST_F(DefaultCredentialsProviderChainTest, NoEnvironmentVars) {
  EXPECT_CALL(factories_,
              createInstanceProfileCredentialsProvider(Ref(api_), Ref(time_system_), _));
  DefaultCredentialsProviderChain chain(api_, time_system_, factories_);
}

TEST_F(DefaultCredentialsProviderChainTest, MetadataDisabled) {
  TestEnvironment::setEnvVar("AWS_EC2_METADATA_DISABLED", "true", 1);
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(Ref(api_), Ref(time_system_), _))
      .Times(0);
  DefaultCredentialsProviderChain chain(api_, time_system_, factories_);
}

TEST_F(DefaultCredentialsProviderChainTest, MetadataNotDisabled) {
  TestEnvironment::setEnvVar("AWS_EC2_METADATA_DISABLED", "false", 1);
  EXPECT_CALL(factories_,
              createInstanceProfileCredentialsProvider(Ref(api_), Ref(time_system_), _));
  DefaultCredentialsProviderChain chain(api_, time_system_, factories_);
}

TEST_F(DefaultCredentialsProviderChainTest, RelativeUri) {
  TestEnvironment::setEnvVar("AWS_CONTAINER_CREDENTIALS_RELATIVE_URI", "/path/to/creds", 1);
  EXPECT_CALL(factories_, createTaskRoleCredentialsProvider(Ref(api_), Ref(time_system_), _,
                                                            "169.254.170.2:80/path/to/creds",
                                                            absl::optional<std::string>()));
  DefaultCredentialsProviderChain chain(api_, time_system_, factories_);
}

TEST_F(DefaultCredentialsProviderChainTest, FullUriNoAuthorizationToken) {
  TestEnvironment::setEnvVar("AWS_CONTAINER_CREDENTIALS_FULL_URI", "http://host/path/to/creds", 1);
  EXPECT_CALL(factories_, createTaskRoleCredentialsProvider(Ref(api_), Ref(time_system_), _,
                                                            "http://host/path/to/creds",
                                                            absl::optional<std::string>()));
  DefaultCredentialsProviderChain chain(api_, time_system_, factories_);
}

TEST_F(DefaultCredentialsProviderChainTest, FullUriWithAuthorizationToken) {
  TestEnvironment::setEnvVar("AWS_CONTAINER_CREDENTIALS_FULL_URI", "http://host/path/to/creds", 1);
  TestEnvironment::setEnvVar("AWS_CONTAINER_AUTHORIZATION_TOKEN", "auth_token", 1);
  EXPECT_CALL(factories_, createTaskRoleCredentialsProvider(
                              Ref(api_), Ref(time_system_), _, "http://host/path/to/creds",
                              absl::optional<std::string>("auth_token")));
  DefaultCredentialsProviderChain chain(api_, time_system_, factories_);
}

} // namespace Auth
} // namespace Aws
} // namespace Envoy