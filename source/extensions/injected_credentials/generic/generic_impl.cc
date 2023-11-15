#include "source/extensions/injected_credentials/generic/generic_impl.h"

namespace Envoy {
namespace Extensions {
namespace Credentials {
namespace Generic {

void GenericCredentialInjector::inject(Http::RequestHeaderMap& headers, bool overwrite) {
  if (!overwrite && !headers.get(Http::LowerCaseString(header_)).empty()) {
    throw EnvoyException("Credential already exists in the header.");
  }

  if (secret_reader_->credential().empty()) {
    throw EnvoyException("Failed to get credential from secret.");
  }

  headers.setCopy(Http::LowerCaseString(header_), secret_reader_->credential());
}

} // namespace Generic
} // namespace Credentials
} // namespace Extensions
} // namespace Envoy
