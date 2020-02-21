#pragma once

#include <unordered_map>

#include "envoy/common/exception.h"
#include "envoy/runtime/runtime.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Utility {

/**
 * ExtensionNameUtil provides utilities for extension names.
 */
class ExtensionNameUtil {
public:
  enum class Status { Warn, Block };

  /**
   * Returns the status of deprecated extension names.
   *
   * @return Status indicating whether deprecated names trigger warnings or are blocked.
   */
  static Status deprecatedExtensionNameStatus(
      Runtime::Loader* runtime = Runtime::LoaderSingleton::getExisting()) {

#ifdef ENVOY_DISABLE_DEPRECATED_FEATURES
    UNREFERENCED_PARAMETER(runtime);
    return Status::Block;
#else
    bool warn_only = true;

    if (runtime && !runtime->snapshot().deprecatedFeatureEnabled(
                       "envoy.deprecated_features.allow_deprecated_extension_names", true)) {
      warn_only = false;
    }

    return warn_only ? Status::Warn : Status::Block;
#endif
  }

  /**
   * Checks the status of deprecated extension and either generates a log message or throws.
   * The passed strings are used only to generate the log or exception message.
   *
   * @throw EnvoyException if the use of deprecated extension names is not allowed.
   */
  static void
  checkDeprecatedExtensionName(absl::string_view extension_type, absl::string_view deprecated_name,
                               absl::string_view canonical_name,
                               Runtime::Loader* runtime = Runtime::LoaderSingleton::getExisting()) {
    auto status = deprecatedExtensionNameStatus(runtime);

    std::string err = fmt::format(
        "Using deprecated {} extension name '{}' for '{}'. This name will be removed from Envoy "
        "soon. Please see "
        "https://www.envoyproxy.io/docs/envoy/latest/intro/deprecated for details.",
        extension_type, deprecated_name, canonical_name);

    if (status == Status::Warn) {
      ENVOY_LOG_MISC(warn, "{}", err);
      return;
    }

    const char fatal_error[] =
        " If continued use of this filter name is absolutely necessary, see "
        "https://www.envoyproxy.io/docs/envoy/latest/configuration/operations/runtime"
        "#using-runtime-overrides-for-deprecated-features for how to apply a temporary and "
        "highly discouraged override.";

    throw EnvoyException(err + fatal_error);
  }
};

} // namespace Utility
} // namespace Common
} // namespace Extensions
} // namespace Envoy
