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
using KeySourceProto = envoy::extensions::filters::http::api_key_auth::v3::KeySource;
using ForwardingProto = envoy::extensions::filters::http::api_key_auth::v3::Forwarding;

// Credentials is a map of API key to client ID.
using Credentials = absl::flat_hash_map<std::string, std::string>;

/**
 * The sources to get the API key from the incoming request.
 */
class KeySources {
public:
  KeySources(const Protobuf::RepeatedPtrField<KeySourceProto>& proto_config,
             absl::Status& creation_status);

  /**
   * To get the API key from the incoming request.
   * @param headers the incoming request headers.
   * @param buffer the buffer that used to store the API key value that parsed from query or
   *        cookie.
   * @return the result string view of getting the API key. The string view will reference to
   *         HTTP request header value or the input buffer.
   */
  absl::string_view getKey(const Http::RequestHeaderMap& headers, std::string& buffer) const;

  /**
   * To remove the API key from the request.
   * @param headers the incoming request headers.
   */
  void removeKey(Http::RequestHeaderMap& headers) const;

  /**
   * To check if the sources are empty.
   */
  bool empty() const { return key_sources_.empty(); }

private:
  class Source {
  public:
    Source(absl::string_view header, absl::string_view query, absl::string_view cookie,
           absl::Status& creation_status);
    absl::string_view getKey(const Http::RequestHeaderMap& headers, std::string& buffer) const;
    void removeKey(Http::RequestHeaderMap& headers) const;

  private:
    absl::variant<Http::LowerCaseString, std::string> source_{""};
    bool query_source_{};
  };

  std::vector<Source> key_sources_;
};

/**
 * Configuration for forwarding client identity upstream and optionally hiding the API key.
 */
class Forwarding {
public:
  Forwarding(const ForwardingProto& proto_config);

  const Http::LowerCaseString& headerName() const { return header_name_; }
  bool hideCredentials() const { return hide_credentials_; }

private:
  Http::LowerCaseString header_name_{""};
  bool hide_credentials_{false};
};

/**
 * The parsed configuration for API key auth. This class is shared by the filter configuration
 * and the route configuration.
 */
struct ApiKeyAuthConfig {
public:
  template <class ProtoType>
  ApiKeyAuthConfig(const ProtoType& proto_config, absl::Status& creation_status)
      : key_sources_(proto_config.key_sources(), creation_status),
        forwarding_(proto_config.forwarding()) {
    RETURN_ONLY_IF_NOT_OK_REF(creation_status);

    credentials_.reserve(proto_config.credentials().size());
    for (const auto& credential : proto_config.credentials()) {
      if (credentials_.contains(credential.key())) {
        creation_status = absl::InvalidArgumentError(
            fmt::format("Duplicated credential key: '{}'", credential.key()));
        return;
      }
      credentials_[credential.key()] = credential.client();
    }
  }

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
    return !credentials_.empty() ? makeOptRef<const Credentials>(credentials_)
                                 : OptRef<const Credentials>{};
  }

  /**
   * To get the optional reference of the client forwarding configuration.
   */
  OptRef<const Forwarding> forwarding() const {
    return forwarding_.has_value() ? makeOptRef(*forwarding_) : OptRef<const Forwarding>{};
  }

private:
  const KeySources key_sources_;
  Credentials credentials_;
  absl::optional<Forwarding> forwarding_;
};

class RouteConfig : public Router::RouteSpecificFilterConfig {
public:
  RouteConfig(const ApiKeyAuthPerRouteProto& proto_config, absl::Status& creation_status);

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

  /**
   * To get the optional reference of the forwarding configuration. If this returns a valid
   * reference, then it will will override the default configuration.
   */
  OptRef<const Forwarding> forwarding() const { return override_config_.forwarding(); }

private:
  const ApiKeyAuthConfig override_config_;
  const absl::flat_hash_set<std::string> allowed_clients_;
};

struct AuthResult {
  bool authenticated{};
  bool authorized{};
  absl::string_view response_code_details{};
};

class FilterConfig {
public:
  FilterConfig(const ApiKeyAuthProto& proto_config, Stats::Scope& scope,
               const std::string& stats_prefix, absl::Status& creation_status);

  /**
   * To get the optional reference of the default credentials.
   */
  OptRef<const Credentials> credentials() const { return default_config_.credentials(); }

  /**
   * To get the optional reference of the default key sources.
   */
  OptRef<const KeySources> keySources() const { return default_config_.keySources(); }

  /**
   * To get the optional reference of the forwarding configuration.
   */
  OptRef<const Forwarding> forwarding() const { return default_config_.forwarding(); }

  /**
   * To get the stats of the filter.
   */
  ApiKeyAuthStats& stats() { return stats_; }

private:
  static ApiKeyAuthStats generateStats(Stats::Scope& scope, const std::string& prefix) {
    return ApiKeyAuthStats{ALL_API_KEY_AUTH_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }

  const ApiKeyAuthConfig default_config_;
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
