#include "source/extensions/common/aws/credential_provider_chains/custom_credentials_provider_chain.h"
#include "source/extensions/common/aws/credential_providers/inline_credentials_provider.h"
#include "test/extensions/common/aws/mocks.h"
#include "test/test_common/environment.h"
#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Ref;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

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
