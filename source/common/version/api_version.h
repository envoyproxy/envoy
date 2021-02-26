#pragma once

#include <string>

// Declares the ApiVersion struct and defines Envoy::api_version.
#include "common/version/api_version_number.h"

namespace Envoy {

class ApiVersionInfoTestPeer;

/**
 * Wraps compiled in api versioning.
 */
class ApiVersionInfo {
public:
  // Returns the most recent API version that is supported by the client.
  static const ApiVersion& apiVersion();

  // Returns the oldest API version that is supported by the client.
  static const ApiVersion& oldestApiVersion();

private:
  friend class Envoy::ApiVersionInfoTestPeer;
  static ApiVersion computeOldestApiVersion(const ApiVersion& latest_version);
};

} // namespace Envoy
