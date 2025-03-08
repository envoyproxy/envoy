#include "source/extensions/filters/http/basic_auth/basic_auth_filter.h"

#include "envoy/http/header_map.h"

#include "source/common/common/base64.h"
#include "source/common/config/utility.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/extensions/hash/factory.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BasicAuth {

namespace {
constexpr uint32_t MaximumUriLength = 256;
} // namespace

FilterConfig::FilterConfig(UserMap&& users, const std::string& forward_username_header,
                           const std::string& authentication_header,
                           const std::string& stats_prefix, Stats::Scope& scope)
    : users_(std::move(users)), forward_username_header_(forward_username_header),
      authentication_header_(Http::LowerCaseString(authentication_header)),
      stats_(generateStats(stats_prefix + "basic_auth.", scope)) {}

BasicAuthFilter::BasicAuthFilter(FilterConfigConstSharedPtr config) : config_(std::move(config)) {
  auto* factory = Envoy::Config::Utility::getFactoryByName<
      Envoy::Extensions::Hash::NamedAlgorithmProviderConfigFactory>("envoy.hash.sha1");
  if (factory == nullptr) {
    throw EnvoyException("basic auth: did not find factory named 'envoy.hash.sha1'");
  }
  hash_algorithm_provider_ = factory->createAlgorithmProvider();
}

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

  if (!validateUser(*users, username, password)) {
    return onDenied("User authentication failed. Invalid username/password combination.",
                    "invalid_credential_for_basic_auth");
  }

  if (!config_->forwardUsernameHeader().empty()) {
    headers.setCopy(Http::LowerCaseString(config_->forwardUsernameHeader()), username);
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

  auto password_hash = hash_algorithm_provider_->computeHash(password);
  auto encoded_hash =
      Base64::encode(password_hash.c_str(), hash_algorithm_provider_->digestLength());
  return encoded_hash == user->second.hash;
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
