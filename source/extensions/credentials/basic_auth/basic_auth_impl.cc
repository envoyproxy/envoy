#include "source/extensions/credentials/basic_auth/basic_auth_impl.h"

#include "source/common/common/base64.h"

namespace Envoy {
namespace Extensions {
namespace Credentials {
namespace BasicAuth {

bool BasicAuthCredentialInjector::inject(Http::RequestHeaderMap& headers, bool overwrite) {
  if (!overwrite && !headers.get(Http::LowerCaseString("Authorization")).empty()) {
    return false;
  }

  const std::string username_password = username_ + ":" + secret_reader_->credential();
  const std::string encoded = Base64::encode(username_password.c_str(), username_password.size());
  headers.setCopy(Http::LowerCaseString("Authorization"), "Basic " + encoded);
  return true;
}

} // namespace BasicAuth
} // namespace Credentials
} // namespace Extensions
} // namespace Envoy
