#pragma once

#include "envoy/extensions/filters/http/api_key_auth/v3/api_key_auth.pb.h"
#include "envoy/extensions/filters/http/api_key_auth/v3/api_key_auth.pb.validate.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ApiKeyAuth {

/**
 * All Basic Auth filter stats. @see stats_macros.h
 */
#define ALL_API_KEY_AUTH_STATS(COUNTER)                                                            \
  COUNTER(allowed)                                                                                 \
  COUNTER(unauthorized)                                                                            \
  COUNTER(forbidden)

/**
 * Struct definition for API key auth stats. @see stats_macros.h
 */
struct ApiKeyAuthStats {
  ALL_API_KEY_AUTH_STATS(GENERATE_COUNTER_STRUCT)
};

using ApiKeyAuthProto = envoy::extensions::filters::http::api_key_auth::v3::ApiKeyAuth;
using ApiKeyAuthPerScopeProto =
    envoy::extensions::filters::http::api_key_auth::v3::ApiKeyAuthPerScope;

using ApiKeyMap = absl::flat_hash_map<std::string, std::string>;

class KeyResult {
public:
  absl::string_view key_string_view;
  bool multiple_keys_error{false};
};

class KeySource {
public:
  KeySource(absl::string_view header, absl::string_view query, absl::string_view cookie);

  KeyResult getApiKey(const Http::RequestHeaderMap& headers, std::string& key_buffer) const;
  bool valid() const { return !header_.get().empty() || !query_.empty() || !cookie_.empty(); }

private:
  const Http::LowerCaseString header_{""};
  const std::string query_;
  const std::string cookie_;
};

struct ApiKeyAuthConfig {
public:
  ApiKeyAuthConfig(const ApiKeyAuthProto& proto_config);

  OptRef<const KeySource> keySource() const {
    return key_source_.valid() ? makeOptRef(key_source_) : OptRef<const KeySource>{};
  }
  OptRef<const ApiKeyMap> apiKeyMap() const {
    return api_key_map_.has_value() ? makeOptRef<const ApiKeyMap>(api_key_map_.value())
                                    : OptRef<const ApiKeyMap>{};
  }

private:
  const KeySource key_source_;
  absl::optional<ApiKeyMap> api_key_map_;
};

class ScopeConfig : public Router::RouteSpecificFilterConfig {
public:
  ScopeConfig(const ApiKeyAuthPerScopeProto& proto_config);

  OptRef<const ApiKeyMap> apiKeyMap() const { return override_config_.apiKeyMap(); }
  OptRef<const KeySource> keySource() const { return override_config_.keySource(); }

  bool allowClient(absl::string_view client_id) const {
    return allowed_clients_.empty() || allowed_clients_.contains(client_id);
  }

private:
  ApiKeyAuthConfig override_config_;
  absl::flat_hash_set<std::string> allowed_clients_;
};

struct AuthResult {
  bool authenticated{};
  bool authorized{};
  absl::string_view response_code_details{};
};

class FilterConfig {
public:
  FilterConfig(const ApiKeyAuthProto& proto_config, Stats::Scope& scope,
               const std::string& stats_prefix);

  OptRef<const ApiKeyMap> apiKeyMap() const { return default_config_.apiKeyMap(); }
  OptRef<const KeySource> keySource() const { return default_config_.keySource(); }

  ApiKeyAuthStats& stats() { return stats_; }

private:
  static ApiKeyAuthStats generateStats(Stats::Scope& scope, const std::string& prefix) {
    return ApiKeyAuthStats{ALL_API_KEY_AUTH_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }

  ApiKeyAuthConfig default_config_;
  ApiKeyAuthStats stats_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

// The Envoy filter to process HTTP api key auth.
class ApiKeyAuthFilter : public Http::PassThroughDecoderFilter,
                         public Logger::Loggable<Logger::Id::basic_auth> {
public:
  ApiKeyAuthFilter(FilterConfigSharedPtr config);

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override;

private:
  Http::FilterHeadersStatus onDenied(Http::Code code, absl::string_view body,
                                     absl::string_view response_code_details);

  FilterConfigSharedPtr config_;
};

} // namespace ApiKeyAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
