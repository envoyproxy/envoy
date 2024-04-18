#pragma once

#include "source/common/version/api_version_struct.h"

// Defines the ApiVersion current version (Envoy::api_version), and oldest
// version (Envoy::oldest_api_version).
#include "source/common/version/api_version_number.h"

namespace Envoy {

/**
 * Wraps compiled in api versioning.
 */
class ApiVersionInfo {
public:
  // Returns the most recent API version that is supported by the client.
  static constexpr ApiVersion apiVersion() { return api_version; }

  // Returns the oldest API version that is supported by the client.
  static constexpr ApiVersion oldestApiVersion() { return oldest_api_version; }
};

} // namespace Envoy
