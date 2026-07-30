#include "source/extensions/http/injected_credentials/generic/generic_impl.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

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

  // Secret files commonly end with a trailing newline, which is not allowed in HTTP header
  // values. Strip trailing CR/LF so the injected header is valid.
  absl::string_view credential = secret_reader_->credential();
  while (!credential.empty() && (credential.back() == '\n' || credential.back() == '\r')) {
    credential.remove_suffix(1);
  }

  if (credential.empty()) {
    return absl::NotFoundError("Failed to get credential from secret");
  }

  headers.setCopy(header_, absl::StrCat(header_value_prefix_, credential));
  return absl::OkStatus();
}

} // namespace Generic
} // namespace InjectedCredentials
} // namespace Http
} // namespace Extensions
} // namespace Envoy
