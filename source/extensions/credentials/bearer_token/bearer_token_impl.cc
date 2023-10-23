#include "source/extensions/credentials/bearer_token/bearer_token_impl.h"

#include "source/common/common/base64.h"

namespace Envoy {
namespace Extensions {
namespace Credentials {
namespace BearerToken {

bool BearerTokenCredentialInjector::inject(Http::RequestHeaderMap& headers, bool overrite) {
  if (!overrite && !headers.get(Http::LowerCaseString("Authorization")).empty()) {
    return false;
  }

  headers.setCopy(Http::LowerCaseString("Authorization"),
                  "Bearer " + secret_reader_->bearer_token());
  return true;
}

} // namespace BearerToken
} // namespace Credentials
} // namespace Extensions
} // namespace Envoy
