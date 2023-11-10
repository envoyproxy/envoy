#include "source/extensions/filters/http/basic_auth/basic_auth_filter.h"

#include <openssl/sha.h>

#include "source/common/common/base64.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BasicAuth {

namespace {

// Function to compute SHA1 hash
std::string computeSHA1(absl::string_view password) {
  unsigned char hash[SHA_DIGEST_LENGTH];

  // Calculate the SHA-1 hash
  SHA1(reinterpret_cast<const unsigned char*>(password.data()), password.length(), hash);

  // Encode the binary hash in Base64
  return Base64::encode(reinterpret_cast<const char*>(hash), SHA_DIGEST_LENGTH);
}

} // namespace

FilterConfig::FilterConfig(UserMap&& users, const std::string& stats_prefix, Stats::Scope& scope)
    : users_(std::move(users)), stats_(generateStats(stats_prefix + "basic_auth.", scope)) {}

bool FilterConfig::validateUser(absl::string_view username, absl::string_view password) const {
  auto user = users_.find(username);
  if (user == users_.end()) {
    return false;
  }

  return computeSHA1(password) == user->second.hash;
}

BasicAuthFilter::BasicAuthFilter(FilterConfigConstSharedPtr config) : config_(std::move(config)) {}

Http::FilterHeadersStatus BasicAuthFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  auto auth_header = headers.get(Http::CustomHeaders::get().Authorization);

  if (auth_header.empty()) {
    return onDenied("User authentication failed. Missing username and password.",
                    "no_credential_for_basic_auth");
  }

  absl::string_view auth_value = auth_header[0]->value().getStringView();

  if (!absl::StartsWith(auth_value, "Basic ")) {
    return onDenied("User authentication failed. Expected 'Basic' authentication scheme.",
                    "invalid_scheme_for_basic_auth");
  }

  // Extract and decode the Base64 part of the header.
  absl::string_view base64_token = auth_value.substr(6);
  const std::string decoded = Base64::decodeWithoutPadding(base64_token);

  // The decoded string is in the format "username:password".
  const size_t colon_pos = decoded.find(':');
  if (colon_pos == std::string::npos) {
    return onDenied("User authentication failed. Invalid basic credential format.",
                    "invalid_format_for_basic_auth");
  }

  absl::string_view decoded_view = decoded;
  absl::string_view username = decoded_view.substr(0, colon_pos);
  absl::string_view password = decoded_view.substr(colon_pos + 1);

  if (!config_->validateUser(username, password)) {
    return onDenied("User authentication failed. Invalid username/password combination.",
                    "invalid_credential_for_basic_auth");
  }

  config_->stats().allowed_.inc();
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus BasicAuthFilter::onDenied(absl::string_view body,
                                                    absl::string_view response_code_details) {
  config_->stats().denied_.inc();
  decoder_callbacks_->sendLocalReply(Http::Code::Unauthorized, body, nullptr, absl::nullopt,
                                     response_code_details);
  return Http::FilterHeadersStatus::StopIteration;
}

} // namespace BasicAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
