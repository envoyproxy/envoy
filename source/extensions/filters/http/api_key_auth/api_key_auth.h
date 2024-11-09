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
using ApiKeyAuthPerRouteProto =
    envoy::extensions::filters::http::api_key_auth::v3::ApiKeyAuthPerRoute;
using KeySourcesProto = envoy::extensions::filters::http::api_key_auth::v3::KeySources;

// Credentials is a map of API key to client ID.
using Credentials = absl::flat_hash_map<std::string, std::string>;

/**
 * The sources to get the API key from the incoming request.
 */
class KeySources {
public:
  KeySources(const KeySourcesProto& proto_config);

  /**
   * To get the API key from the incoming request.
   * @param headers the incoming request headers.
   * @param key_buffer the buffer to store the API key.
   * @return the result of getting the API key.
   */
  absl::string_view getKey(const Http::RequestHeaderMap& headers, std::string& buffer) const;

  /**
   * To check if the sources are empty.
   */
  bool empty() const { return key_sources_.empty(); }

private:
  class Source {
  public:
    Source(absl::string_view header, absl::string_view query, absl::string_view cookie);
    absl::string_view getKey(const Http::RequestHeaderMap& headers, std::string& buffer) const;

  private:
    absl::variant<Http::LowerCaseString, std::string> source_{""};
    bool query_source_{};
  };

  std::vector<Source> key_sources_;
};

/**
 * The parsed configuration for API key auth. This class is shared by the filter configuration
 * and the route configuration.
 */
struct ApiKeyAuthConfig {
public:
  ApiKeyAuthConfig(const ApiKeyAuthProto& proto_config);

  /**
   * To get the optional reference of the key sources.
   */
  OptRef<const KeySources> keySources() const {
    return !key_sources_.empty() ? makeOptRef(key_sources_) : OptRef<const KeySources>{};
  }

  /**
   * To get the optional reference of the credentials.
   */
  OptRef<const Credentials> credentials() const {
    return credentials_.has_value() ? makeOptRef<const Credentials>(credentials_.value())
                                    : OptRef<const Credentials>{};
  }

private:
  const KeySources key_sources_;
  absl::optional<Credentials> credentials_;
};

class RouteConfig : public Router::RouteSpecificFilterConfig {
public:
  RouteConfig(const ApiKeyAuthPerRouteProto& proto_config);

  /**
   * To get the optional reference of the credentials. If this returns an valid reference, then
   * the credentials will override the default credentials.
   */
  OptRef<const Credentials> credentials() const { return override_config_.credentials(); }

  /**
   * To get the optional reference of the key sources. If this returns an valid reference, then
   * the key sources will override the default key sources.
   */
  OptRef<const KeySources> keySources() const { return override_config_.keySources(); }

  /**
   * To check if the client is allowed.
   * @param client the client ID to check.
   * @return true if the client is allowed, otherwise false.
   */
  bool allowClient(absl::string_view client) const {
    return allowed_clients_.empty() || allowed_clients_.contains(client);
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

  /**
   * To get the optional reference of the default credentials.
   */
  OptRef<const Credentials> credentials() const { return default_config_.credentials(); }

  /**
   * To get the optional reference of the default key sources.
   */
  OptRef<const KeySources> keySources() const { return default_config_.keySources(); }

  /**
   * To get the stats of the filter.
   */
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
