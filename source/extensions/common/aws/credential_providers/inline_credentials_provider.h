#pragma once
#include "source/extensions/common/aws/credentials_provider.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

/**
 * Credential provider based on an inline credential.
 */
class InlineCredentialProvider : public CredentialsProvider {
public:
  explicit InlineCredentialProvider(absl::string_view access_key_id,
                                    absl::string_view secret_access_key,
                                    absl::string_view session_token)
      : credentials_(access_key_id, secret_access_key, session_token) {}

  Credentials getCredentials() override { return credentials_; }
  bool credentialsPending() override { return false; };
  std::string providerName() override { return "InlineCredentialsProvider"; };

private:
  const Credentials credentials_;
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
