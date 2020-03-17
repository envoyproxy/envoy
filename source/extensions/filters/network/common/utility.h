#pragma once

#include <unordered_map>

#include "common/common/macros.h"

#include "extensions/common/utility.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {

/**
 * FilterNameUtil provides utilities for handling Network filter
 * extension names (e.g., "envoy.filters.network.redis_proxy").
 */
class FilterNameUtil {
public:
  /**
   * Given a deprecated network filter extension name, return the
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
        "network filter", name, it->second, runtime);

    return it->second;
  }

private:
  using DeprecatedNameMap = absl::flat_hash_map<std::string, std::string>;

  static const DeprecatedNameMap& deprecatedNameMap() {
    CONSTRUCT_ON_FIRST_USE(
        DeprecatedNameMap,
        {
            {"envoy.redis_proxy", NetworkFilters::NetworkFilterNames::get().RedisProxy},
        });
  }
};

} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
