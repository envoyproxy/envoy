#pragma once

#include <unordered_map>

#include "envoy/common/exception.h"
#include "envoy/runtime/runtime.h"

#include "common/common/documentation_url.h"
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
   * Checks the status of deprecated extension names and increments the deprecated feature stats
   * counter if deprecated names are allowed.
   *
   * @param runtime Runtime::Loader used to determine if deprecated extension names are allowed.
   * @return Status::Warn (allowed, warn) or Status::Block (disallowed, error)
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
   * Checks the status of deprecated extension names. If deprecated extension names are allowed,
   * it increments the deprecated feature stats counter. Generates a warning or error log message
   * based on whether the name is allowed (warning) or not (error). The string parameters are used
   * only to generate the log message.
   *
   * @param extension_type absl::string_view that contains the extension type, for logging
   * @param deprecated_name absl::string_view that contains the deprecated name, for logging
   * @param canonical_name absl::string_view that contains the canonical name, for logging
   * @param runtime Runtime::Loader used to determine if deprecated extension names are allowed.
   * @return true if deprecated extensions are allowed, false otherwise.
   */
  static bool
  allowDeprecatedExtensionName(absl::string_view extension_type, absl::string_view deprecated_name,
                               absl::string_view canonical_name,
                               Runtime::Loader* runtime = Runtime::LoaderSingleton::getExisting()) {
    auto status = deprecatedExtensionNameStatus(runtime);

    if (status == Status::Warn) {
      ENVOY_LOG_MISC(warn, "{}", message(extension_type, deprecated_name, canonical_name));
      return true;
    }

    ENVOY_LOG_MISC(error, "{}", fatalMessage(extension_type, deprecated_name, canonical_name));
    return false;
  }

  /**
   * Checks the status of deprecated extension names. If deprecated extension names are allowed,
   * it increments the deprecated feature stats counter and generates a log message. If not allowed,
   * an exception is thrown. The passed strings are used only to generate the log or exception
   * message.
   *
   * @param extension_type absl::string_view that contains the extension type, for logging
   * @param deprecated_name absl::string_view that contains the deprecated name, for logging
   * @param canonical_name absl::string_view that contains the canonical name, for logging
   * @param runtime Runtime::Loader used to determine if deprecated extension names are allowed.
   * @throw EnvoyException if the use of deprecated extension names is not allowed.
   */
  static void
  checkDeprecatedExtensionName(absl::string_view extension_type, absl::string_view deprecated_name,
                               absl::string_view canonical_name,
                               Runtime::Loader* runtime = Runtime::LoaderSingleton::getExisting()) {
    auto status = deprecatedExtensionNameStatus(runtime);

    if (status == Status::Warn) {
      ENVOY_LOG_MISC(warn, "{}", message(extension_type, deprecated_name, canonical_name));
      return;
    }

    throw EnvoyException(fatalMessage(extension_type, deprecated_name, canonical_name));
  }

private:
  static std::string message(absl::string_view extension_type, absl::string_view deprecated_name,
                             absl::string_view canonical_name) {
    absl::string_view spacing = extension_type.empty() ? "" : " ";

    return fmt::format(
        "Using deprecated {}{}extension name '{}' for '{}'. This name will be removed from Envoy "
        "soon. Please see {} for details.",
        extension_type, spacing, deprecated_name, canonical_name, ENVOY_DOC_URL_VERSION_HISTORY);
  }

  static std::string fatalMessage(absl::string_view extension_type,
                                  absl::string_view deprecated_name,
                                  absl::string_view canonical_name) {
    std::string err = message(extension_type, deprecated_name, canonical_name);

    const char fatal_error[] = " If continued use of this filter name is absolutely necessary, "
                               "see " ENVOY_DOC_URL_RUNTIME_OVERRIDE_DEPRECATED " for "
                               "how to apply a temporary and highly discouraged override.";

    return err + fatal_error;
  }
};

} // namespace Utility
} // namespace Common
} // namespace Extensions
} // namespace Envoy
