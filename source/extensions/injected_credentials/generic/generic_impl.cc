#include "source/extensions/injected_credentials/generic/generic_impl.h"

namespace Envoy {
namespace Extensions {
namespace Credentials {
namespace Generic {

bool GenericCredentialInjector::inject(Http::RequestHeaderMap& headers, bool overwrite) {
  if (!overwrite && !headers.get(Http::LowerCaseString(header_)).empty()) {
    ENVOY_LOG(warn, "Credential already exists in the header.");
    return false;
  }

  if (secret_reader_->credential().empty()) {
    ENVOY_LOG(warn, "Credential is empty.");
    return false;
  }

  headers.setCopy(Http::LowerCaseString(header_), secret_reader_->credential());
  return true;
}

} // namespace Generic
} // namespace Credentials
} // namespace Extensions
} // namespace Envoy
