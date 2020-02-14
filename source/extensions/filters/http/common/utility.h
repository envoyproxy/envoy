#pragma once

#include <unordered_map>

#include "envoy/runtime/runtime.h"

#include "common/common/logger.h"
#include "common/common/macros.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {

/**
 * FilterNameUtil provides utilities for handling HTTP filter
 * extension names (e.g., "envoy.filters.http.buffer").
 */
class FilterNameUtil {
public:
  /**
   * Given a deprecated HTTP filter extension name, return the
   * canonical name. Any name not defined in the deprecated map is
   * returned without modification.
   */
  static const std::string&
  canonicalFilterName(const std::string& name,
                      Runtime::Loader* runtime = Runtime::LoaderSingleton::getExisting()) {
    const auto& it = deprecatedNameMap().find(name);
    if (it == deprecatedNameMap().end()) {
      return name;
    }

    std::string err = fmt::format(
        "Using deprecated http filter name '{}' for '{}'. This name will be removed from Envoy "
        "soon. Please see "
        "https://www.envoyproxy.io/docs/envoy/latest/intro/deprecated for details.",
        name, it->second);

#ifdef ENVOY_DISABLE_DEPRECATED_FEATURES
    bool warn_only = false;
#else
    bool warn_only = true;
#endif

    if (runtime && !runtime->snapshot().deprecatedFeatureEnabled(
                       "envoy.deprecated_features.allow_historic_http_filter_names", true)) {
      warn_only = false;
    }

    if (warn_only) {
      ENVOY_LOG_MISC(warn, "{}", err);
    } else {
      const char fatal_error[] =
          " If continued use of this filter name is absolutely necessary, see "
          "https://www.envoyproxy.io/docs/envoy/latest/configuration/operations/runtime"
          "#using-runtime-overrides-for-deprecated-features for how to apply a temporary and "
          "highly discouraged override.";

      throw EnvoyException(err + fatal_error);
    }

    return it->second;
  }

private:
  using DeprecatedNameMap = absl::flat_hash_map<std::string, std::string>;

  static const DeprecatedNameMap& deprecatedNameMap() {
    CONSTRUCT_ON_FIRST_USE(
        DeprecatedNameMap,
        {
            {"envoy.buffer", HttpFilters::HttpFilterNames::get().Buffer},
            {"envoy.cors", HttpFilters::HttpFilterNames::get().Cors},
            {"envoy.csrf", HttpFilters::HttpFilterNames::get().Csrf},
            {"envoy.ext_authz", HttpFilters::HttpFilterNames::get().ExtAuthorization},
            {"envoy.fault", HttpFilters::HttpFilterNames::get().Fault},
            {"envoy.grpc_http1_bridge", HttpFilters::HttpFilterNames::get().GrpcHttp1Bridge},
            {"envoy.grpc_json_transcoder", HttpFilters::HttpFilterNames::get().GrpcJsonTranscoder},
            {"envoy.grpc_web", HttpFilters::HttpFilterNames::get().GrpcWeb},
            {"envoy.gzip", HttpFilters::HttpFilterNames::get().EnvoyGzip},
            {"envoy.health_check", HttpFilters::HttpFilterNames::get().HealthCheck},
            {"envoy.http_dynamic_filter", HttpFilters::HttpFilterNames::get().Dynamo},
            {"envoy.ip_tagging", HttpFilters::HttpFilterNames::get().IpTagging},
            {"envoy.lua", HttpFilters::HttpFilterNames::get().Lua},
            {"envoy.rate_limit", HttpFilters::HttpFilterNames::get().RateLimit},
            {"envoy.router", HttpFilters::HttpFilterNames::get().Router},
            {"envoy.squash", HttpFilters::HttpFilterNames::get().Squash},
        });
  }
};

} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
