#include "source/extensions/injected_credentials/generic/generic_impl.h"

namespace Envoy {
namespace Extensions {
namespace InjectedCredentials {
namespace Generic {

absl::Status GenericCredentialInjector::inject(Http::RequestHeaderMap& headers, bool overwrite) {
  if (!overwrite && !headers.get(Http::LowerCaseString(header_)).empty()) {
    return absl::AlreadyExistsError("Credential already exists in the header");
  }

  if (secret_reader_->credential().empty()) {
    return absl::NotFoundError("Failed to get credential from secret");
  }

  headers.setCopy(Http::LowerCaseString(header_), secret_reader_->credential());
  return absl::OkStatus();
}

} // namespace Generic
} // namespace InjectedCredentials
} // namespace Extensions
} // namespace Envoy
