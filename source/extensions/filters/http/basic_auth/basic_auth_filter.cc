#include "source/extensions/filters/http/basic_auth/basic_auth_filter.h"

#include <openssl/sha.h>

#include "envoy/http/header_map.h"

#include "source/common/common/base64.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BasicAuth {

namespace {
constexpr uint32_t MaximumUriLength = 256;
constexpr absl::string_view DynamicMetadataNamespace = "envoy.filters.http.basic_auth";
constexpr absl::string_view DynamicMetadataUsernameKey = "username";

// Function to compute SHA1 hash
std::string computeSHA1(absl::string_view password) {
  unsigned char hash[SHA_DIGEST_LENGTH];

  // Calculate the SHA-1 hash
  SHA1(reinterpret_cast<const unsigned char*>(password.data()), password.length(), hash);

  // Encode the binary hash in Base64
  return Base64::encode(reinterpret_cast<const char*>(hash), SHA_DIGEST_LENGTH);
}

} // namespace

FilterConfig::FilterConfig(UserMap&& users, const std::string& forward_username_header,
                           const std::string& authentication_header, bool allow_missing,
                           bool emit_dynamic_metadata, const std::string& stats_prefix,
                           Stats::Scope& scope)
    : users_(std::move(users)), forward_username_header_(forward_username_header),
      authentication_header_(Http::LowerCaseString(authentication_header)),
      allow_missing_(allow_missing), emit_dynamic_metadata_(emit_dynamic_metadata),
      stats_(generateStats(stats_prefix + "basic_auth.", scope)) {}

BasicAuthFilter::BasicAuthFilter(FilterConfigConstSharedPtr config) : config_(std::move(config)) {}

Http::FilterHeadersStatus BasicAuthFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  const auto* route_specific_settings =
      Http::Utility::resolveMostSpecificPerFilterConfig<FilterConfigPerRoute>(decoder_callbacks_);
  const UserMap* users = &config_->users();
  if (route_specific_settings != nullptr) {
    users = &route_specific_settings->users();
  }

  Http::HeaderMap::GetResult auth_header;
  if (!config_->authenticationHeader().get().empty()) {
    auth_header = headers.get(config_->authenticationHeader());
  } else {
    auth_header = headers.get(Http::CustomHeaders::get().Authorization);
  }

  if (auth_header.empty()) {
    if (config_->allowMissing()) {
      return Http::FilterHeadersStatus::Continue;
    }
    return onDenied("User authentication failed. Missing username and password.",
                    "no_credential_for_basic_auth");
  }

  absl::string_view auth_value = auth_header[0]->value().getStringView();

  if (!absl::StartsWith(auth_value, "Basic ")) {
    if (config_->allowMissing()) {
      return Http::FilterHeadersStatus::Continue;
    }
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

  if (!validateUser(*users, username, password)) {
    return onDenied("User authentication failed. Invalid username/password combination.",
                    "invalid_credential_for_basic_auth");
  }

  if (!config_->forwardUsernameHeader().empty()) {
    headers.setCopy(Http::LowerCaseString(config_->forwardUsernameHeader()), username);
  }

  if (config_->emitDynamicMetadata()) {
    setDynamicMetadata(username);
  }
  config_->stats().allowed_.inc();
  return Http::FilterHeadersStatus::Continue;
}

bool BasicAuthFilter::validateUser(const UserMap& users, absl::string_view username,
                                   absl::string_view password) const {
  auto user = users.find(username);
  if (user == users.end()) {
    return false;
  }

  return computeSHA1(password) == user->second.hash;
}

void BasicAuthFilter::setDynamicMetadata(absl::string_view username) {
  ProtobufWkt::Struct metadata;
  (*metadata.mutable_fields())[std::string(DynamicMetadataUsernameKey)].set_string_value(
      std::string(username));
  decoder_callbacks_->streamInfo().setDynamicMetadata(std::string(DynamicMetadataNamespace),
                                                      metadata);
}

Http::FilterHeadersStatus BasicAuthFilter::onDenied(absl::string_view body,
                                                    absl::string_view response_code_details) {
  config_->stats().denied_.inc();
  decoder_callbacks_->sendLocalReply(
      Http::Code::Unauthorized, body,
      [this](Http::ResponseHeaderMap& headers) {
        // requestHeaders should always be non-null at this point since onDenied is only called by
        // decodeHeaders.
        const auto request_headers = this->decoder_callbacks_->requestHeaders();
        const std::string uri = Http::Utility::buildOriginalUri(*request_headers, MaximumUriLength);
        const std::string value = absl::StrCat("Basic realm=\"", uri, "\"");
        headers.setReferenceKey(Http::Headers::get().WWWAuthenticate, value);
      },
      absl::nullopt, response_code_details);
  return Http::FilterHeadersStatus::StopIteration;
}

} // namespace BasicAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
