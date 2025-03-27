#pragma once
#include "source/common/common/logger.h"
#include "source/extensions/common/aws/credentials_provider.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

/**
 * Retrieve AWS credentials from the environment variables.
 *
 * Adheres to conventions specified in:
 * https://docs.aws.amazon.com/cli/latest/userguide/cli-configure-envvars.html
 */
class EnvironmentCredentialsProvider : public CredentialsProvider,
                                       public Logger::Loggable<Logger::Id::aws> {
public:
  Credentials getCredentials() override;
  bool credentialsPending() override { return false; };
  std::string providerName() override { return "EnvironmentCredentialsProvider"; };
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
