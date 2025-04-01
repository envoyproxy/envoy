#pragma once

#include "source/common/common/logger.h"
#include "source/extensions/common/aws/credentials_provider.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

/**
 * Returns AWS credentials from static filter configuration.
 *
 * Adheres to conventions specified in:
 * https://docs.aws.amazon.com/cli/latest/userguide/cli-configure-envvars.html
 */
class ConfigCredentialsProvider : public CredentialsProvider,
                                  public Logger::Loggable<Logger::Id::aws> {
public:
  ConfigCredentialsProvider(absl::string_view access_key_id = absl::string_view(),
                            absl::string_view secret_access_key = absl::string_view(),
                            absl::string_view session_token = absl::string_view())
      : credentials_(access_key_id, secret_access_key, session_token) {}
  Credentials getCredentials() override;
  bool credentialsPending() override { return false; };
  std::string providerName() override { return "ConfigCredentialsProvider"; };

private:
  const Credentials credentials_;
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
