#include "source/extensions/common/aws/credential_provider_chains.h"
#include "source/extensions/common/aws/credential_providers/inline_credentials_provider.h"

#include "test/extensions/common/aws/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"

#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Ref;
using testing::WithArg;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

class DefaultCredentialsProviderChainTest : public testing::Test {
public:
  DefaultCredentialsProviderChainTest() : api_(Api::createApiForTest(time_system_)) {
    ON_CALL(context_, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
    cluster_manager_.initializeThreadLocalClusters({"credentials_provider_cluster"});
    EXPECT_CALL(factories_, createEnvironmentCredentialsProvider());
  }

  void SetUp() override {
    // Implicit environment clear for each DefaultCredentialsProviderChainTest
    TestEnvironment::unsetEnvVar("AWS_CONTAINER_CREDENTIALS_RELATIVE_URI");
    TestEnvironment::unsetEnvVar("AWS_CONTAINER_CREDENTIALS_FULL_URI");
    TestEnvironment::unsetEnvVar("AWS_CONTAINER_AUTHORIZATION_TOKEN");
    TestEnvironment::unsetEnvVar("AWS_CONTAINER_AUTHORIZATION_TOKEN_FILE");
    TestEnvironment::unsetEnvVar("AWS_EC2_METADATA_DISABLED");
    TestEnvironment::unsetEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE");
    TestEnvironment::unsetEnvVar("AWS_ROLE_ARN");
    TestEnvironment::unsetEnvVar("AWS_ROLE_SESSION_NAME");
  }

  TestScopedRuntime scoped_runtime_;
  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<MockCredentialsProviderChainFactories> factories_;
};

TEST_F(DefaultCredentialsProviderChainTest, NoEnvironmentVars) {
  EXPECT_CALL(factories_, mockCreateCredentialsFileCredentialsProvider(Ref(context_), _));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(Ref(*api_), _, _, _, _, _, _));
  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};

  DefaultCredentialsProviderChain chain(*api_, context_, "region", credential_provider_config,
                                        factories_);
}

TEST_F(DefaultCredentialsProviderChainTest, MetadataDisabled) {
  TestEnvironment::setEnvVar("AWS_EC2_METADATA_DISABLED", "true", 1);
  EXPECT_CALL(factories_, mockCreateCredentialsFileCredentialsProvider(Ref(context_), _));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(Ref(*api_), _, _, _, _, _, _))
      .Times(0);
  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};

  DefaultCredentialsProviderChain chain(*api_, context_, "region", credential_provider_config,
                                        factories_);
}

TEST_F(DefaultCredentialsProviderChainTest, MetadataNotDisabled) {
  TestEnvironment::setEnvVar("AWS_EC2_METADATA_DISABLED", "false", 1);
  EXPECT_CALL(factories_, mockCreateCredentialsFileCredentialsProvider(Ref(context_), _));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(Ref(*api_), _, _, _, _, _, _));
  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};

  DefaultCredentialsProviderChain chain(*api_, context_, "region", credential_provider_config,
                                        factories_);
}

TEST_F(DefaultCredentialsProviderChainTest, RelativeUri) {
  TestEnvironment::setEnvVar("AWS_CONTAINER_CREDENTIALS_RELATIVE_URI", "/path/to/creds", 1);
  EXPECT_CALL(factories_, mockCreateCredentialsFileCredentialsProvider(Ref(context_), _));
  EXPECT_CALL(factories_, createContainerCredentialsProvider(
                              Ref(*api_), _, _, _, _, "169.254.170.2:80/path/to/creds", _, _, ""));
  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};

  DefaultCredentialsProviderChain chain(*api_, context_, "region", credential_provider_config,
                                        factories_);
}

TEST_F(DefaultCredentialsProviderChainTest, FullUriNoAuthorizationToken) {
  TestEnvironment::setEnvVar("AWS_CONTAINER_CREDENTIALS_FULL_URI", "http://host/path/to/creds", 1);
  EXPECT_CALL(factories_, mockCreateCredentialsFileCredentialsProvider(Ref(context_), _));
  EXPECT_CALL(factories_, createContainerCredentialsProvider(
                              Ref(*api_), _, _, _, _, "http://host/path/to/creds", _, _, ""));
  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};

  DefaultCredentialsProviderChain chain(*api_, context_, "region", credential_provider_config,
                                        factories_);
}

TEST_F(DefaultCredentialsProviderChainTest, FullUriWithAuthorizationToken) {
  TestEnvironment::setEnvVar("AWS_CONTAINER_CREDENTIALS_FULL_URI", "http://host/path/to/creds", 1);
  TestEnvironment::setEnvVar("AWS_CONTAINER_AUTHORIZATION_TOKEN", "auth_token", 1);
  EXPECT_CALL(factories_, mockCreateCredentialsFileCredentialsProvider(Ref(context_), _));
  EXPECT_CALL(factories_,
              createContainerCredentialsProvider(Ref(*api_), _, _, _, _,
                                                 "http://host/path/to/creds", _, _, "auth_token"));
  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};

  DefaultCredentialsProviderChain chain(*api_, context_, "region", credential_provider_config,
                                        factories_);
}

TEST_F(DefaultCredentialsProviderChainTest, NoWebIdentityRoleArn) {
  TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
  EXPECT_CALL(factories_, mockCreateCredentialsFileCredentialsProvider(Ref(context_), _));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(Ref(*api_), _, _, _, _, _, _));
  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};

  DefaultCredentialsProviderChain chain(*api_, context_, "region", credential_provider_config,
                                        factories_);
}

TEST_F(DefaultCredentialsProviderChainTest, NoWebIdentitySessionName) {
  TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_ARN", "aws:iam::123456789012:role/arn", 1);
  time_system_.setSystemTime(std::chrono::milliseconds(1234567890));
  EXPECT_CALL(factories_, mockCreateCredentialsFileCredentialsProvider(Ref(context_), _));
  EXPECT_CALL(factories_, createWebIdentityCredentialsProvider(Ref(context_), _, _, _));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(Ref(*api_), _, _, _, _, _, _));
  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};

  DefaultCredentialsProviderChain chain(*api_, context_, "region", credential_provider_config,
                                        factories_);
}

TEST_F(DefaultCredentialsProviderChainTest, WebIdentityWithSessionName) {
  TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_ARN", "aws:iam::123456789012:role/arn", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_SESSION_NAME", "role-session-name", 1);
  EXPECT_CALL(factories_, mockCreateCredentialsFileCredentialsProvider(Ref(context_), _));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(Ref(*api_), _, _, _, _, _, _));
  EXPECT_CALL(factories_, createWebIdentityCredentialsProvider(Ref(context_), _, _, _));

  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};

  DefaultCredentialsProviderChain chain(*api_, context_, "region", credential_provider_config,
                                        factories_);
}

TEST_F(DefaultCredentialsProviderChainTest, NoWebIdentityWithBlankConfig) {
  TestEnvironment::unsetEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE");
  TestEnvironment::unsetEnvVar("AWS_ROLE_ARN");
  EXPECT_CALL(factories_, mockCreateCredentialsFileCredentialsProvider(Ref(context_), _));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(Ref(*api_), _, _, _, _, _, _));
  EXPECT_CALL(factories_, createWebIdentityCredentialsProvider(Ref(context_), _, _, _)).Times(0);

  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};

  DefaultCredentialsProviderChain chain(*api_, context_, "region", credential_provider_config,
                                        factories_);
}
// These tests validate override of default credential provider with custom credential provider
// settings

TEST_F(DefaultCredentialsProviderChainTest, WebIdentityWithCustomSessionName) {
  TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_ARN", "aws:iam::123456789012:role/arn", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_SESSION_NAME", "role-session-name", 1);
  EXPECT_CALL(factories_, mockCreateCredentialsFileCredentialsProvider(Ref(context_), _));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(Ref(*api_), _, _, _, _, _, _));

  std::string role_session_name;

  EXPECT_CALL(factories_, createWebIdentityCredentialsProvider(Ref(context_), _, _, _))
      .WillOnce(Invoke(WithArg<3>(
          [&role_session_name](
              const envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider&
                  provider) -> CredentialsProviderSharedPtr {
            role_session_name = provider.role_session_name();
            return nullptr;
          })));

  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};
  credential_provider_config.mutable_assume_role_with_web_identity_provider()
      ->set_role_session_name("custom-role-session-name");

  DefaultCredentialsProviderChain chain(*api_, context_, "region", credential_provider_config,
                                        factories_);
  EXPECT_EQ(role_session_name, "custom-role-session-name");
}

TEST_F(DefaultCredentialsProviderChainTest, WebIdentityWithCustomRoleArn) {
  TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_ARN", "aws:iam::123456789012:role/arn", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_SESSION_NAME", "role-session-name", 1);
  EXPECT_CALL(factories_, mockCreateCredentialsFileCredentialsProvider(Ref(context_), _));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(Ref(*api_), _, _, _, _, _, _));

  std::string role_arn;

  EXPECT_CALL(factories_, createWebIdentityCredentialsProvider(Ref(context_), _, _, _))
      .WillOnce(Invoke(WithArg<3>(
          [&role_arn](
              const envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider&
                  provider) -> CredentialsProviderSharedPtr {
            role_arn = provider.role_arn();
            return nullptr;
          })));

  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};
  credential_provider_config.mutable_assume_role_with_web_identity_provider()->set_role_arn(
      "custom-role-arn");

  DefaultCredentialsProviderChain chain(*api_, context_, "region", credential_provider_config,
                                        factories_);
  EXPECT_EQ(role_arn, "custom-role-arn");
}

TEST_F(DefaultCredentialsProviderChainTest, WebIdentityWithCustomDataSource) {
  TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_ARN", "aws:iam::123456789012:role/arn", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_SESSION_NAME", "role-session-name", 1);
  EXPECT_CALL(factories_, mockCreateCredentialsFileCredentialsProvider(Ref(context_), _));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(Ref(*api_), _, _, _, _, _, _));

  std::string inline_string;

  EXPECT_CALL(factories_, createWebIdentityCredentialsProvider(Ref(context_), _, _, _))
      .WillOnce(Invoke(WithArg<3>(
          [&inline_string](
              const envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider&
                  provider) -> CredentialsProviderSharedPtr {
            inline_string = provider.web_identity_token_data_source().inline_string();
            return nullptr;
          })));

  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};
  credential_provider_config.mutable_assume_role_with_web_identity_provider()
      ->mutable_web_identity_token_data_source()
      ->set_inline_string("custom_token_string");

  DefaultCredentialsProviderChain chain(*api_, context_, "region", credential_provider_config,
                                        factories_);
  EXPECT_EQ(inline_string, "custom_token_string");
}

TEST_F(DefaultCredentialsProviderChainTest, CredentialsFileWithCustomDataSource) {
  TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_ARN", "aws:iam::123456789012:role/arn", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_SESSION_NAME", "role-session-name", 1);

  std::string inline_string;

  EXPECT_CALL(factories_, mockCreateCredentialsFileCredentialsProvider(Ref(context_), _))
      .WillOnce(Invoke(WithArg<1>(
          [&inline_string](
              const envoy::extensions::common::aws::v3::CredentialsFileCredentialProvider& provider)
              -> CredentialsProviderSharedPtr {
            inline_string = provider.credentials_data_source().inline_string();
            return nullptr;
          })));

  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(Ref(*api_), _, _, _, _, _, _));

  EXPECT_CALL(factories_, createWebIdentityCredentialsProvider(Ref(context_), _, _, _));

  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};
  credential_provider_config.mutable_credentials_file_provider()
      ->mutable_credentials_data_source()
      ->set_inline_string("custom_inline_string");

  DefaultCredentialsProviderChain chain(*api_, context_, "region", credential_provider_config,
                                        factories_);
  EXPECT_EQ(inline_string, "custom_inline_string");
}

class CustomCredentialsProviderChainTest : public testing::Test {};

TEST_F(CustomCredentialsProviderChainTest, CreateFileCredentialProviderOnly) {
  NiceMock<MockCustomCredentialsProviderChainFactories> factories;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context;
  auto region = "ap-southeast-2";
  auto file_path = TestEnvironment::writeStringToFileForTest("credentials", "hello");

  envoy::extensions::common::aws::v3::AwsCredentialProvider cred_provider = {};
  cred_provider.mutable_credentials_file_provider()
      ->mutable_credentials_data_source()
      ->set_filename(file_path);

  EXPECT_CALL(factories, mockCreateCredentialsFileCredentialsProvider(Ref(server_context), _));
  EXPECT_CALL(factories, createWebIdentityCredentialsProvider(Ref(server_context), _, _, _))
      .Times(0);

  auto chain = std::make_shared<Extensions::Common::Aws::CustomCredentialsProviderChain>(
      server_context, region, cred_provider, factories);
}

TEST_F(CustomCredentialsProviderChainTest, CreateWebIdentityCredentialProviderOnly) {
  NiceMock<MockCustomCredentialsProviderChainFactories> factories;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context;
  auto region = "ap-southeast-2";
  auto file_path = TestEnvironment::writeStringToFileForTest("credentials", "hello");

  envoy::extensions::common::aws::v3::AwsCredentialProvider cred_provider = {};
  cred_provider.mutable_assume_role_with_web_identity_provider()->set_role_arn("arn://1234");
  cred_provider.mutable_assume_role_with_web_identity_provider()
      ->mutable_web_identity_token_data_source()
      ->set_filename(file_path);

  EXPECT_CALL(factories, mockCreateCredentialsFileCredentialsProvider(Ref(server_context), _))
      .Times(0);
  EXPECT_CALL(factories, createWebIdentityCredentialsProvider(Ref(server_context), _, _, _));

  auto chain = std::make_shared<Extensions::Common::Aws::CustomCredentialsProviderChain>(
      server_context, region, cred_provider, factories);
}

TEST_F(CustomCredentialsProviderChainTest, WebIdentityNoEnvironmentSession) {
  NiceMock<MockCustomCredentialsProviderChainFactories> factories;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context;
  Event::SimulatedTimeSystem time_system;

  TestEnvironment::unsetEnvVar("AWS_ROLE_SESSION_NAME");
  time_system.setSystemTime(std::chrono::milliseconds(1234567890));

  auto region = "ap-southeast-2";
  auto file_path = TestEnvironment::writeStringToFileForTest("credentials", "hello");

  envoy::extensions::common::aws::v3::AwsCredentialProvider cred_provider = {};
  cred_provider.mutable_assume_role_with_web_identity_provider()->set_role_arn("arn://1234");
  cred_provider.mutable_assume_role_with_web_identity_provider()
      ->mutable_web_identity_token_data_source()
      ->set_filename(file_path);

  EXPECT_CALL(factories, mockCreateCredentialsFileCredentialsProvider(Ref(server_context), _))
      .Times(0);
  std::string role_session_name;

  EXPECT_CALL(factories, createWebIdentityCredentialsProvider(Ref(server_context), _, _, _))
      .WillOnce(Invoke(WithArg<3>(
          [&role_session_name](
              const envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider&
                  provider) -> CredentialsProviderSharedPtr {
            role_session_name = provider.role_session_name();
            return nullptr;
          })));

  auto chain = std::make_shared<Extensions::Common::Aws::CustomCredentialsProviderChain>(
      server_context, region, cred_provider, factories);
  // Role session name is equal to nanoseconds from the set simulated system time when environment
  // variable is unset
  EXPECT_EQ(role_session_name, "1234567890000000");
}

TEST_F(CustomCredentialsProviderChainTest, CreateFileAndWebProviders) {
  NiceMock<MockCustomCredentialsProviderChainFactories> factories;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context;
  auto region = "ap-southeast-2";
  auto file_path = TestEnvironment::writeStringToFileForTest("credentials", "hello");

  envoy::extensions::common::aws::v3::AwsCredentialProvider cred_provider = {};
  cred_provider.mutable_credentials_file_provider()
      ->mutable_credentials_data_source()
      ->set_filename(file_path);
  cred_provider.mutable_assume_role_with_web_identity_provider()->set_role_arn("arn://1234");
  cred_provider.mutable_assume_role_with_web_identity_provider()
      ->mutable_web_identity_token_data_source()
      ->set_filename(file_path);

  EXPECT_CALL(factories, mockCreateCredentialsFileCredentialsProvider(Ref(server_context), _));
  EXPECT_CALL(factories, createWebIdentityCredentialsProvider(Ref(server_context), _, _, _));

  auto chain = std::make_shared<Extensions::Common::Aws::CustomCredentialsProviderChain>(
      server_context, region, cred_provider, factories);
}

TEST(CreateCredentialsProviderFromConfig, InlineCredential) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  envoy::extensions::common::aws::v3::InlineCredentialProvider inline_credential;
  inline_credential.set_access_key_id("TestAccessKey");
  inline_credential.set_secret_access_key("TestSecret");
  inline_credential.set_session_token("TestSessionToken");

  envoy::extensions::common::aws::v3::AwsCredentialProvider base;
  base.mutable_inline_credential()->CopyFrom(inline_credential);

  auto provider = std::make_shared<Extensions::Common::Aws::InlineCredentialProvider>(
      inline_credential.access_key_id(), inline_credential.secret_access_key(),
      inline_credential.session_token());
  const absl::StatusOr<Credentials> creds = provider->getCredentials();
  EXPECT_EQ("TestAccessKey", creds->accessKeyId().value());
  EXPECT_EQ("TestSecret", creds->secretAccessKey().value());
  EXPECT_EQ("TestSessionToken", creds->sessionToken().value());
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
