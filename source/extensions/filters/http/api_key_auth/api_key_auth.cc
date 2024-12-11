#include "source/extensions/filters/http/api_key_auth/api_key_auth.h"

#include <openssl/sha.h>

#include "envoy/http/header_map.h"

#include "source/common/common/base64.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ApiKeyAuth {

RouteConfig::RouteConfig(const ApiKeyAuthPerRouteProto& proto)
    : override_config_(proto),
      allowed_clients_(proto.allowed_clients().begin(), proto.allowed_clients().end()) {}

KeySources::Source::Source(absl::string_view header, absl::string_view query,
                           absl::string_view cookie) {
  if (!header.empty()) {
    source_ = Http::LowerCaseString(header);
  } else if (!query.empty()) {
    source_ = std::string(query);
    query_source_ = true;
  } else if (!cookie.empty()) {
    source_ = std::string(cookie);
  } else {
    throw EnvoyException("One of 'header'/'query'/'cookie' must be set.");
  }
}

KeySources::KeySources(const Protobuf::RepeatedPtrField<KeySourceProto>& proto_config) {
  key_sources_.reserve(proto_config.size());
  for (const auto& source : proto_config) {
    key_sources_.emplace_back(source.header(), source.query(), source.cookie());
  }
}

absl::string_view KeySources::Source::getKey(const Http::RequestHeaderMap& headers,
                                             std::string& buffer) const {
  if (absl::holds_alternative<Http::LowerCaseString>(source_)) {
    if (const auto header = headers.get(absl::get<Http::LowerCaseString>(source_));
        !header.empty()) {
      absl::string_view header_view = header[0]->value().getStringView();
      if (absl::StartsWith(header_view, "Bearer ")) {
        header_view = header_view.substr(7);
      }
      return header_view;
    }
  } else if (query_source_) {
    auto params =
        Http::Utility::QueryParamsMulti::parseAndDecodeQueryString(headers.getPathValue());
    if (auto iter = params.data().find(absl::get<std::string>(source_));
        iter != params.data().end()) {
      if (!iter->second.empty()) {
        buffer = std::move(iter->second[0]);
        return buffer;
      }
    }
  } else {
    buffer = Http::Utility::parseCookieValue(headers, absl::get<std::string>(source_));
    return buffer;
  }

  return {};
}

absl::string_view KeySources::getKey(const Http::RequestHeaderMap& headers,
                                     std::string& buffer) const {
  for (const auto& source : key_sources_) {
    if (auto key = source.getKey(headers, buffer); !key.empty()) {
      return key;
    }
  }
  return {};
}

FilterConfig::FilterConfig(const ApiKeyAuthProto& proto_config, Stats::Scope& scope,
                           const std::string& stats_prefix)
    : default_config_(proto_config), stats_(generateStats(scope, stats_prefix + "api_key_auth.")) {}

ApiKeyAuthFilter::ApiKeyAuthFilter(FilterConfigSharedPtr config) : config_(std::move(config)) {}

Http::FilterHeadersStatus ApiKeyAuthFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  const RouteConfig* route_config =
      Http::Utility::resolveMostSpecificPerFilterConfig<RouteConfig>(decoder_callbacks_);

  OptRef<const Credentials> credentials = config_->credentials();
  OptRef<const KeySources> key_sources = config_->keySources();

  // If there is an override config, then try to override the API key map and key source.
  if (route_config != nullptr) {
    if (OptRef<const Credentials> route_credentials = route_config->credentials();
        route_credentials.has_value()) {
      credentials = route_credentials;
    }
    if (OptRef<const KeySources> route_key_sources = route_config->keySources();
        route_key_sources.has_value()) {
      key_sources = route_key_sources;
    }
  }

  if (!key_sources.has_value()) {
    return onDenied(Http::Code::Unauthorized, "Client authentication failed.",
                    "missing_key_sources");
  }
  if (!credentials.has_value()) {
    return onDenied(Http::Code::Unauthorized, "Client authentication failed.",
                    "missing_credentials");
  }

  std::string key_buffer;
  absl::string_view key_result = key_sources->getKey(headers, key_buffer);

  if (key_result.empty()) {
    return onDenied(Http::Code::Unauthorized, "Client authentication failed.", "missing_api_key");
  }

  const auto credential = credentials->find(key_result);
  if (credential == credentials->end()) {
    return onDenied(Http::Code::Unauthorized, "Client authentication failed.", "unkonwn_api_key");
  }

  // If route config is not null then check if the client is allowed or not based on the route
  // configuration.
  if (route_config != nullptr) {
    if (!route_config->allowClient(credential->second)) {
      return onDenied(Http::Code::Forbidden, "Client is forbidden.", "client_not_allowed");
    }
  }

  config_->stats().allowed_.inc();
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus ApiKeyAuthFilter::onDenied(Http::Code code, absl::string_view body,
                                                     absl::string_view response_code_details) {
  if (code == Http::Code::Unauthorized) {
    config_->stats().unauthorized_.inc();
  } else {
    config_->stats().forbidden_.inc();
  }

  decoder_callbacks_->sendLocalReply(code, body, nullptr, absl::nullopt, response_code_details);
  return Http::FilterHeadersStatus::StopIteration;
}

} // namespace ApiKeyAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
