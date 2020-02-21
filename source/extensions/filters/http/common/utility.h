#pragma once

#include <unordered_map>

#include "common/common/macros.h"

#include "extensions/common/utility.h"
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
   * returned without modification. If deprecated extension names are
   * disabled, throws EnvoyException.
   *
   * @return const std::string& canonical filter name
   * @throw EnvoyException if deprecated names are disabled
   */
  static const std::string&
  canonicalFilterName(const std::string& name,
                      Runtime::Loader* runtime = Runtime::LoaderSingleton::getExisting()) {
    const auto& it = deprecatedNameMap().find(name);
    if (it == deprecatedNameMap().end()) {
      return name;
    }

    Extensions::Common::Utility::ExtensionNameUtil::checkDeprecatedExtensionName(
        "http filter", name, it->second, runtime);

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
