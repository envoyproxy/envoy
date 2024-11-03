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

ApiKeyAuthConfig::ApiKeyAuthConfig(const ApiKeyAuthProto& proto_config) {
  if (proto_config.has_credentials()) {
    ApiKeyMap api_key_map;
    api_key_map.reserve(proto_config.credentials().entries_size());
    for (const auto& credential : proto_config.credentials().entries()) {
      if (api_key_map.contains(credential.api_key())) {
        throw EnvoyException("Duplicate API key.");
      }
      api_key_map[credential.api_key()] = credential.client_id();
    }

    api_key_map_ = std::move(api_key_map);
  }

  if (!proto_config.authentication_header().empty()) {
    key_source_ = KeySource{};
    key_source_->header_key = Http::LowerCaseString(proto_config.authentication_header());
  }

  if (!proto_config.authentication_query().empty()) {
    if (!key_source_.has_value()) {
      key_source_ = KeySource{};
    }
    key_source_.value().query_key = proto_config.authentication_query();
  }

  if (!proto_config.authentication_cookie().empty()) {
    if (!key_source_.has_value()) {
      key_source_ = KeySource{};
    }
    key_source_.value().cookie_key = proto_config.authentication_cookie();
  }
}

ScopeConfig::ScopeConfig(const ApiKeyAuthPerScopeProto& proto)
    : override_config_(proto.override_config()) {
  allowed_clients_.insert(proto.allowed_clients().begin(), proto.allowed_clients().end());
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
                    "missing_api_key_map");
  }

  std::string api_key_string;
  absl::string_view api_key_string_view;
  bool saw_api_key{false};

  // Try to get the API key from the header first if the header key is not empty.
  if (!key_source->header_key.get().empty()) {
    const auto auth_header = headers.get(key_source->header_key);
    if (auth_header.size() > 1) {
      return onDenied(Http::Code::Unauthorized, "Client authentication failed.",
                      "multiple_api_key");
    }
    if (!auth_header.empty()) {
      absl::string_view auth_header_view = auth_header[0]->value().getStringView();
      if (absl::StartsWith(auth_header_view, "Bearer ")) {
        auth_header_view = auth_header_view.substr(7);
      }
      api_key_string_view = auth_header_view;
      saw_api_key = true;
    }
  }

  // If the API key is not found in the header, try to get it from the query parameter
  // if the query key is not empty.
  if (!saw_api_key && !key_source->query_key.empty()) {
    auto query_params =
        Http::Utility::QueryParamsMulti::parseAndDecodeQueryString(headers.getPathValue());
    auto iter = query_params.data().find(key_source->query_key);
    if (iter->second.size() > 1) {
      return onDenied(Http::Code::Unauthorized, "Client authentication failed.",
                      "multiple_api_key");
    }
    if (!iter->second.empty()) {
      api_key_string = std::move(iter->second[0]);
      api_key_string_view = api_key_string;
      saw_api_key = true;
    }
  }

  // If the API key is not found in the header and query parameter, try to get it from the cookie
  // if the cookie key is not empty.
  if (!saw_api_key && !key_source->cookie_key.empty()) {
    api_key_string = Http::Utility::parseCookieValue(headers, key_source->cookie_key);
    api_key_string_view = api_key_string;
    saw_api_key = !api_key_string_view.empty();
  }

  if (!saw_api_key) {
    return onDenied(Http::Code::Unauthorized, "Client authentication failed.", "missing_api_key");
  }

  const auto credential = api_key_map->find(api_key_string_view);
  if (credential == api_key_map->end()) {
    return onDenied(Http::Code::Unauthorized, "Client authentication failed.", "unkonwn_api_key");
  }

  if (override_config != nullptr) {
    if (!override_config->allowClient(credential->second)) {
      return onDenied(Http::Code::Forbidden, "Client authentication failed.", "forbidden_api_key");
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
