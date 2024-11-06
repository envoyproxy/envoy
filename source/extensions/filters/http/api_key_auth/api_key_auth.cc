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

ApiKeyAuthConfig::ApiKeyAuthConfig(const ApiKeyAuthProto& proto_config)
    : key_source_(proto_config.authentication_header(), proto_config.authentication_query(),
                  proto_config.authentication_cookie()) {
  if (!proto_config.has_credentials()) {
    return;
  }

  ApiKeyMap api_key_map;
  api_key_map.reserve(proto_config.credentials().entries_size());

  for (const auto& credential : proto_config.credentials().entries()) {
    if (api_key_map.contains(credential.api_key())) {
      throwEnvoyExceptionOrPanic("Duplicate API key.");
    }
    api_key_map[credential.api_key()] = credential.client_id();
  }

  api_key_map_ = std::move(api_key_map);
}

ScopeConfig::ScopeConfig(const ApiKeyAuthPerScopeProto& proto)
    : override_config_(proto.override_config()) {
  allowed_clients_.insert(proto.allowed_clients().begin(), proto.allowed_clients().end());
}

KeySource::KeySource(absl::string_view header, absl::string_view query, absl::string_view cookie)
    : header_(header), query_(query), cookie_(cookie) {}

KeyResult KeySource::getApiKey(const Http::RequestHeaderMap& headers,
                               std::string& key_buffer) const {
  // Try to get the API key from the header first if the header key is not empty.
  if (!header_.get().empty()) {
    if (const auto auth_header = headers.get(header_); !auth_header.empty()) {
      if (auth_header.size() > 1) {
        return {{}, true};
      }

      absl::string_view auth_header_view = auth_header[0]->value().getStringView();
      if (absl::StartsWith(auth_header_view, "Bearer ")) {
        auth_header_view = auth_header_view.substr(7);
      }
      return {auth_header_view};
    }
  }

  // If the API key is not found in the header, try to get it from the query parameter
  // if the query key is not empty.
  if (!query_.empty()) {
    auto query_params =
        Http::Utility::QueryParamsMulti::parseAndDecodeQueryString(headers.getPathValue());
    if (auto iter = query_params.data().find(query_); iter != query_params.data().end()) {
      if (iter->second.size() > 1) {
        return {{}, true};
      }
      if (!iter->second.empty()) {
        key_buffer = std::move(iter->second[0]);
        return {absl::string_view{key_buffer}};
      }
    }
  }

  // If the API key is not found in the header and query parameter, try to get it from the
  // cookie if the cookie key is not empty.
  if (!cookie_.empty()) {
    key_buffer = Http::Utility::parseCookieValue(headers, cookie_);
    return {absl::string_view{key_buffer}};
  }

  return {};
}

FilterConfig::FilterConfig(const ApiKeyAuthProto& proto_config, Stats::Scope& scope,
                           const std::string& stats_prefix)
    : default_config_(proto_config), stats_(generateStats(scope, stats_prefix + "api_key_auth.")) {}

ApiKeyAuthFilter::ApiKeyAuthFilter(FilterConfigSharedPtr config) : config_(std::move(config)) {}

Http::FilterHeadersStatus ApiKeyAuthFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  const ScopeConfig* override_config =
      Http::Utility::resolveMostSpecificPerFilterConfig<ScopeConfig>(decoder_callbacks_);

  OptRef<const ApiKeyMap> api_key_map = config_->apiKeyMap();
  OptRef<const KeySource> key_source = config_->keySource();

  // If there is an override config, then try to override the API key map and key source.
  if (override_config != nullptr) {
    OptRef<const ApiKeyMap> override_api_key_map = override_config->apiKeyMap();
    if (override_api_key_map.has_value()) {
      api_key_map = override_api_key_map;
    }

    OptRef<const KeySource> override_key_source = override_config->keySource();
    if (override_key_source.has_value()) {
      key_source = override_key_source;
    }
  }

  if (!key_source.has_value()) {
    return onDenied(Http::Code::Unauthorized, "Client authentication failed.",
                    "missing_key_source");
  }
  if (!api_key_map.has_value()) {
    return onDenied(Http::Code::Unauthorized, "Client authentication failed.",
                    "missing_credentials");
  }

  std::string key_buffer;
  const KeyResult key_result = key_source->getApiKey(headers, key_buffer);

  if (key_result.multiple_keys_error) {
    return onDenied(Http::Code::Unauthorized, "Client authentication failed.", "multiple_api_key");
  }
  if (key_result.key_string_view.empty()) {
    return onDenied(Http::Code::Unauthorized, "Client authentication failed.", "missing_api_key");
  }

  const auto credential = api_key_map->find(key_result.key_string_view);
  if (credential == api_key_map->end()) {
    return onDenied(Http::Code::Unauthorized, "Client authentication failed.", "unkonwn_api_key");
  }

  if (override_config != nullptr) {
    if (!override_config->allowClient(credential->second)) {
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
