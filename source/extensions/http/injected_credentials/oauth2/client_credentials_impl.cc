#include "source/extensions/http/injected_credentials/oauth2/client_credentials_impl.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace InjectedCredentials {
namespace OAuth2 {

absl::Status OAuth2ClientCredentialTokenInjector::inject(Envoy::Http::RequestHeaderMap& headers,
                                                         bool overwrite) {
  if (!overwrite && !headers.get(Envoy::Http::CustomHeaders::get().Authorization).empty()) {
    return absl::AlreadyExistsError("Credential already exists in the header");
  }

  auto token = token_reader_->credential();

  if (token.empty()) {
    ENVOY_LOG(error, "Failed to get oauth2 token from token provider");
    return absl::NotFoundError("Failed to get oauth2 token from token provider");
  }

  headers.setReferenceKey(Envoy::Http::CustomHeaders::get().Authorization, token);
  return absl::OkStatus();
}

} // namespace OAuth2
} // namespace InjectedCredentials
} // namespace Http
} // namespace Extensions
} // namespace Envoy
