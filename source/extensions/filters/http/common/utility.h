#pragma once

#include "source/common/common/macros.h"
#include "source/extensions/common/utility.h"

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
            {"envoy.buffer", "envoy.filters.http.buffer"},
            {"envoy.cors", "envoy.filters.http.cors"},
            {"envoy.csrf", "envoy.filters.http.csrf"},
            {"envoy.ext_authz", "envoy.filters.http.ext_authz"},
            {"envoy.fault", "envoy.filters.http.fault"},
            {"envoy.grpc_http1_bridge", "envoy.filters.http.grpc_http1_bridge"},
            {"envoy.grpc_json_transcoder", "envoy.filters.http.grpc_json_transcoder"},
            {"envoy.grpc_web", "envoy.filters.http.grpc_web"},
            {"envoy.gzip", "envoy.filters.http.gzip"},
            {"envoy.health_check", "envoy.filters.http.health_check"},
            {"envoy.http_dynamic_filter", "envoy.filters.http.dynamo"},
            {"envoy.ip_tagging", "envoy.filters.http.ip_tagging"},
            {"envoy.lua", "envoy.filters.http.lua"},
            {"envoy.rate_limit", "envoy.filters.http.ratelimit"},
            {"envoy.router", "envoy.filters.http.router"},
            {"envoy.squash", "envoy.filters.http.squash"},
        });
  }
};

} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
