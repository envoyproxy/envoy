#include "source/extensions/http/injected_credentials/generic/generic_impl.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace InjectedCredentials {
namespace Generic {

absl::Status GenericCredentialInjector::inject(Envoy::Http::RequestHeaderMap& headers,
                                               bool overwrite) {
  if (!overwrite && !headers.get(header_).empty()) {
    return absl::AlreadyExistsError("Credential already exists in the header");
  }

  if (secret_reader_->credential().empty()) {
    return absl::NotFoundError("Failed to get credential from secret");
  }

  headers.setCopy(header_, secret_reader_->credential());
  return absl::OkStatus();
}

} // namespace Generic
} // namespace InjectedCredentials
} // namespace Http
} // namespace Extensions
} // namespace Envoy
