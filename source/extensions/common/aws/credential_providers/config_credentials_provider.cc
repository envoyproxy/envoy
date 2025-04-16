#include "source/extensions/common/aws/credential_providers/config_credentials_provider.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

Credentials ConfigCredentialsProvider::getCredentials() {
  ENVOY_LOG(debug, "Getting AWS credentials from static configuration");
  return credentials_;
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
