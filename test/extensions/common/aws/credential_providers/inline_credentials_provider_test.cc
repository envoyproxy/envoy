#include "envoy/extensions/common/aws/v3/credential_provider.pb.h"

#include "source/extensions/common/aws/credential_providers/inline_credentials_provider.h"

#include "test/mocks/server/server_factory_context.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

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

TEST(Coverage, InlineCredential) {
  envoy::extensions::common::aws::v3::InlineCredentialProvider inline_credential;
  inline_credential.set_access_key_id("TestAccessKey");
  inline_credential.set_secret_access_key("TestSecret");
  inline_credential.set_session_token("TestSessionToken");

  auto provider = std::make_shared<Extensions::Common::Aws::InlineCredentialProvider>(
      inline_credential.access_key_id(), inline_credential.secret_access_key(),
      inline_credential.session_token());
  EXPECT_EQ("InlineCredentialsProvider", provider->providerName());
  EXPECT_FALSE(provider->credentialsPending());
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
