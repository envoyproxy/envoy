#include "source/extensions/common/aws/credential_provider_chains.h"

#include "test/extensions/common/aws/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"

#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
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
    mock_provider_ = std::make_shared<MockCredentialsProvider>();
    EXPECT_CALL(factories_, createEnvironmentCredentialsProvider())
        .WillRepeatedly(Return(mock_provider_));
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
  MockCredentialsProviderChainFactories factories_;
  std::shared_ptr<MockCredentialsProvider> mock_provider_;
};

TEST_F(DefaultCredentialsProviderChainTest, NoEnvironmentVars) {
  Envoy::Logger::Registry::setLogLevel(spdlog::level::debug);
  MockCredentialsProvider mock_provider;

  EXPECT_CALL(factories_, mockCreateCredentialsFileCredentialsProvider(Ref(context_), _))
      .WillRepeatedly(Return(mock_provider_));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(_, _, _, _, _, _))
      .WillRepeatedly(Return(mock_provider_));
  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};

  CommonCredentialsProviderChain chain(context_, "region", credential_provider_config, factories_);
  EXPECT_EQ(3, chain.getNumProviders());
}

TEST_F(DefaultCredentialsProviderChainTest, MetadataDisabled) {
  TestEnvironment::setEnvVar("AWS_EC2_METADATA_DISABLED", "true", 1);
  EXPECT_CALL(factories_, mockCreateCredentialsFileCredentialsProvider(Ref(context_), _))
      .WillRepeatedly(Return(mock_provider_));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(_, _, _, _, _, _)).Times(0);
  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};

  CommonCredentialsProviderChain chain(context_, "region", credential_provider_config, factories_);
  EXPECT_EQ(2, chain.getNumProviders());
}

TEST_F(DefaultCredentialsProviderChainTest, MetadataNotDisabled) {
  TestEnvironment::setEnvVar("AWS_EC2_METADATA_DISABLED", "false", 1);
  EXPECT_CALL(factories_, mockCreateCredentialsFileCredentialsProvider(Ref(context_), _))
      .WillRepeatedly(Return(mock_provider_));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(_, _, _, _, _, _))
      .WillRepeatedly(Return(mock_provider_));
  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};

  CommonCredentialsProviderChain chain(context_, "region", credential_provider_config, factories_);
  EXPECT_EQ(3, chain.getNumProviders());
}

TEST_F(DefaultCredentialsProviderChainTest, RelativeUri) {
  TestEnvironment::setEnvVar("AWS_CONTAINER_CREDENTIALS_RELATIVE_URI", "/path/to/creds", 1);
  EXPECT_CALL(factories_, mockCreateCredentialsFileCredentialsProvider(Ref(context_), _))
      .WillRepeatedly(Return(mock_provider_));
  EXPECT_CALL(factories_, createContainerCredentialsProvider(
                              _, _, _, _, "169.254.170.2:80/path/to/creds", _, _, ""))
      .WillRepeatedly(Return(mock_provider_));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(_, _, _, _, _, _))
      .WillRepeatedly(Return(mock_provider_));
  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};

  CommonCredentialsProviderChain chain(context_, "region", credential_provider_config, factories_);
  EXPECT_EQ(4, chain.getNumProviders());
}

TEST_F(DefaultCredentialsProviderChainTest, FullUriNoAuthorizationToken) {
  TestEnvironment::setEnvVar("AWS_CONTAINER_CREDENTIALS_FULL_URI", "http://host/path/to/creds", 1);
  EXPECT_CALL(factories_, mockCreateCredentialsFileCredentialsProvider(Ref(context_), _))
      .WillRepeatedly(Return(mock_provider_));
  EXPECT_CALL(factories_,
              createContainerCredentialsProvider(_, _, _, _, "http://host/path/to/creds", _, _, ""))
      .WillRepeatedly(Return(mock_provider_));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(_, _, _, _, _, _))
      .WillRepeatedly(Return(mock_provider_));
  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};

  CommonCredentialsProviderChain chain(context_, "region", credential_provider_config, factories_);
  EXPECT_EQ(4, chain.getNumProviders());
}

TEST_F(DefaultCredentialsProviderChainTest, FullUriWithAuthorizationToken) {
  TestEnvironment::setEnvVar("AWS_CONTAINER_CREDENTIALS_FULL_URI", "http://host/path/to/creds", 1);
  TestEnvironment::setEnvVar("AWS_CONTAINER_AUTHORIZATION_TOKEN", "auth_token", 1);
  EXPECT_CALL(factories_, mockCreateCredentialsFileCredentialsProvider(Ref(context_), _))
      .WillRepeatedly(Return(mock_provider_));
  EXPECT_CALL(factories_, createContainerCredentialsProvider(
                              _, _, _, _, "http://host/path/to/creds", _, _, "auth_token"))
      .WillRepeatedly(Return(mock_provider_));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(_, _, _, _, _, _))
      .WillRepeatedly(Return(mock_provider_));
  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};

  CommonCredentialsProviderChain chain(context_, "region", credential_provider_config, factories_);
  EXPECT_EQ(4, chain.getNumProviders());
}

TEST_F(DefaultCredentialsProviderChainTest, NoWebIdentityRoleArn) {
  TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
  EXPECT_CALL(factories_, mockCreateCredentialsFileCredentialsProvider(Ref(context_), _))
      .WillRepeatedly(Return(mock_provider_));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(_, _, _, _, _, _))
      .WillRepeatedly(Return(mock_provider_));
  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};

  CommonCredentialsProviderChain chain(context_, "region", credential_provider_config, factories_);
  EXPECT_EQ(3, chain.getNumProviders());
}

TEST_F(DefaultCredentialsProviderChainTest, NoWebIdentitySessionName) {
  TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_ARN", "aws:iam::123456789012:role/arn", 1);
  time_system_.setSystemTime(std::chrono::milliseconds(1234567890));
  EXPECT_CALL(factories_, mockCreateCredentialsFileCredentialsProvider(Ref(context_), _))
      .WillRepeatedly(Return(mock_provider_));
  EXPECT_CALL(factories_, createWebIdentityCredentialsProvider(Ref(context_), _, _, _))
      .WillRepeatedly(Return(mock_provider_));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(_, _, _, _, _, _))
      .WillRepeatedly(Return(mock_provider_));
  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};

  CommonCredentialsProviderChain chain(context_, "region", credential_provider_config, factories_);
  EXPECT_EQ(4, chain.getNumProviders());
}

TEST_F(DefaultCredentialsProviderChainTest, WebIdentityWithSessionName) {
  TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_ARN", "aws:iam::123456789012:role/arn", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_SESSION_NAME", "role-session-name", 1);
  EXPECT_CALL(factories_, mockCreateCredentialsFileCredentialsProvider(Ref(context_), _))
      .WillRepeatedly(Return(mock_provider_));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(_, _, _, _, _, _))
      .WillRepeatedly(Return(mock_provider_));
  EXPECT_CALL(factories_, createWebIdentityCredentialsProvider(Ref(context_), _, _, _))
      .WillRepeatedly(Return(mock_provider_));

  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};

  CommonCredentialsProviderChain chain(context_, "region", credential_provider_config, factories_);
  EXPECT_EQ(4, chain.getNumProviders());
}

TEST_F(DefaultCredentialsProviderChainTest, NoWebIdentityWithBlankConfig) {
  TestEnvironment::unsetEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE");
  TestEnvironment::unsetEnvVar("AWS_ROLE_ARN");
  EXPECT_CALL(factories_, mockCreateCredentialsFileCredentialsProvider(Ref(context_), _))
      .WillRepeatedly(Return(mock_provider_));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(_, _, _, _, _, _))
      .WillRepeatedly(Return(mock_provider_));
  EXPECT_CALL(factories_, createWebIdentityCredentialsProvider(Ref(context_), _, _, _)).Times(0);

  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};

  CommonCredentialsProviderChain chain(context_, "region", credential_provider_config, factories_);
  EXPECT_EQ(3, chain.getNumProviders());
}
// These tests validate override of default credential provider with custom credential provider
// settings

TEST_F(DefaultCredentialsProviderChainTest, WebIdentityWithCustomSessionName) {
  TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_ARN", "aws:iam::123456789012:role/arn", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_SESSION_NAME", "role-session-name", 1);
  EXPECT_CALL(factories_, mockCreateCredentialsFileCredentialsProvider(Ref(context_), _))
      .WillRepeatedly(Return(mock_provider_));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(_, _, _, _, _, _))
      .WillRepeatedly(Return(mock_provider_));

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

  CommonCredentialsProviderChain chain(context_, "region", credential_provider_config, factories_);
  EXPECT_EQ(role_session_name, "custom-role-session-name");
}

TEST_F(DefaultCredentialsProviderChainTest, WebIdentityWithCustomRoleArn) {
  TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_ARN", "aws:iam::123456789012:role/arn", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_SESSION_NAME", "role-session-name", 1);
  EXPECT_CALL(factories_, mockCreateCredentialsFileCredentialsProvider(Ref(context_), _))
      .WillRepeatedly(Return(mock_provider_));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(_, _, _, _, _, _))
      .WillRepeatedly(Return(mock_provider_));

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

  CommonCredentialsProviderChain chain(context_, "region", credential_provider_config, factories_);
  EXPECT_EQ(role_arn, "custom-role-arn");
}

TEST_F(DefaultCredentialsProviderChainTest, WebIdentityWithCustomDataSource) {
  TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_ARN", "aws:iam::123456789012:role/arn", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_SESSION_NAME", "role-session-name", 1);
  EXPECT_CALL(factories_, mockCreateCredentialsFileCredentialsProvider(Ref(context_), _));
  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(_, _, _, _, _, _));

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

  CommonCredentialsProviderChain chain(context_, "region", credential_provider_config, factories_);
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

  EXPECT_CALL(factories_, createInstanceProfileCredentialsProvider(_, _, _, _, _, _));

  EXPECT_CALL(factories_, createWebIdentityCredentialsProvider(Ref(context_), _, _, _));

  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};
  credential_provider_config.mutable_credentials_file_provider()
      ->mutable_credentials_data_source()
      ->set_inline_string("custom_inline_string");

  CommonCredentialsProviderChain chain(context_, "region", credential_provider_config, factories_);
  EXPECT_EQ(inline_string, "custom_inline_string");
}

class CustomCredentialsProviderChainTest : public testing::Test {
public:
  CustomCredentialsProviderChainTest() : api_(Api::createApiForTest(time_system_)) {
    ON_CALL(context_, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
    cluster_manager_.initializeThreadLocalClusters({"credentials_provider_cluster"});
  }

  void SetUp() override {
    // Implicit environment clear for each CustomCredentialsProviderChainTest
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

TEST_F(CustomCredentialsProviderChainTest, NoProvider) {
  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};
  credential_provider_config.set_custom_credential_provider_chain(true);
  auto chain = Envoy::Extensions::Common::Aws::CommonCredentialsProviderChain::
      customCredentialsProviderChain(context_, "region", credential_provider_config);
  EXPECT_FALSE(chain.ok());
}

TEST_F(CustomCredentialsProviderChainTest, InstanceProfileOnly) {
  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};
  credential_provider_config.set_custom_credential_provider_chain(true);
  credential_provider_config.mutable_instance_profile_credential_provider();
  auto chain = Envoy::Extensions::Common::Aws::CommonCredentialsProviderChain::
      customCredentialsProviderChain(context_, "region", credential_provider_config);
  EXPECT_TRUE(chain.ok());
  EXPECT_EQ(1, chain.value()->getNumProviders());
}

TEST_F(CustomCredentialsProviderChainTest, InstanceProfileAndEnvironmentOnly) {
  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};
  credential_provider_config.set_custom_credential_provider_chain(true);
  credential_provider_config.mutable_instance_profile_credential_provider();
  credential_provider_config.mutable_environment_credential_provider();
  auto chain = Envoy::Extensions::Common::Aws::CommonCredentialsProviderChain::
      customCredentialsProviderChain(context_, "region", credential_provider_config);
  EXPECT_TRUE(chain.ok());
  EXPECT_EQ(2, chain.value()->getNumProviders());
}

TEST_F(CustomCredentialsProviderChainTest, WebIdentityOnly) {
  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};
  credential_provider_config.set_custom_credential_provider_chain(true);
  credential_provider_config.mutable_assume_role_with_web_identity_provider()->set_role_arn("arn");
  credential_provider_config.mutable_assume_role_with_web_identity_provider()
      ->mutable_web_identity_token_data_source()
      ->set_environment_variable("TEST");
  auto chain = Envoy::Extensions::Common::Aws::CommonCredentialsProviderChain::
      customCredentialsProviderChain(context_, "region", credential_provider_config);
  EXPECT_TRUE(chain.ok());
  EXPECT_EQ(1, chain.value()->getNumProviders());
}

TEST_F(CustomCredentialsProviderChainTest, CredentialFileOnly) {
  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};
  credential_provider_config.set_custom_credential_provider_chain(true);
  credential_provider_config.mutable_credentials_file_provider();
  auto chain = Envoy::Extensions::Common::Aws::CommonCredentialsProviderChain::
      customCredentialsProviderChain(context_, "region", credential_provider_config);
  EXPECT_TRUE(chain.ok());
  EXPECT_EQ(1, chain.value()->getNumProviders());
}

TEST_F(CustomCredentialsProviderChainTest, ContainerOnly) {
  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};
  TestEnvironment::setEnvVar("AWS_CONTAINER_CREDENTIALS_RELATIVE_URI", "/path/to/creds", 1);

  credential_provider_config.set_custom_credential_provider_chain(true);
  credential_provider_config.mutable_container_credential_provider();
  auto chain = Envoy::Extensions::Common::Aws::CommonCredentialsProviderChain::
      customCredentialsProviderChain(context_, "region", credential_provider_config);
  EXPECT_TRUE(chain.ok());
  EXPECT_EQ(1, chain.value()->getNumProviders());
}

TEST_F(CustomCredentialsProviderChainTest, AssumeRoleOnly) {
  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};
  credential_provider_config.set_custom_credential_provider_chain(true);
  credential_provider_config.mutable_assume_role_credential_provider()->set_role_arn(
      "test-role-arn");
  credential_provider_config.mutable_assume_role_credential_provider()->set_role_session_name(
      "test-session");

  auto chain = Envoy::Extensions::Common::Aws::CommonCredentialsProviderChain::
      customCredentialsProviderChain(context_, "us-east-1", credential_provider_config);
  EXPECT_TRUE(chain.ok());
  EXPECT_EQ(1, chain.value()->getNumProviders());
}

TEST_F(CustomCredentialsProviderChainTest, AssumeRoleWithEnvironment) {
  envoy::extensions::common::aws::v3::AwsCredentialProvider credential_provider_config = {};
  credential_provider_config.set_custom_credential_provider_chain(true);
  credential_provider_config.mutable_assume_role_credential_provider()->set_role_arn(
      "test-role-arn");
  credential_provider_config.mutable_assume_role_credential_provider()->set_role_session_name(
      "test-session");
  credential_provider_config.mutable_environment_credential_provider();

  auto chain = Envoy::Extensions::Common::Aws::CommonCredentialsProviderChain::
      customCredentialsProviderChain(context_, "us-east-1", credential_provider_config);
  EXPECT_TRUE(chain.ok());
  EXPECT_EQ(2, chain.value()->getNumProviders());
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
