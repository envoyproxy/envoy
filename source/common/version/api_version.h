#pragma once

#include <string>

#include "envoy/config/core/v3/api_version.pb.h"

#include "common/version/api_version_number.h"

namespace Envoy {

class ApiVersionInfoTestPeer;

/**
 * Wraps compiled in api versioning.
 */
class ApiVersionInfo {
public:
  // Converts a given API version to a string <Major>.<Minor>.<Patch>.
  static std::string apiVersionToString(const envoy::config::core::v3::ApiVersionNumber& version);

  // Returns the most recent API version that is supported by the client.
  static const envoy::config::core::v3::ApiVersionNumber& apiVersion();

  // Returns the oldest API version that is supported by the client.
  static const envoy::config::core::v3::ApiVersionNumber& oldestApiVersion();

private:
  friend class Envoy::ApiVersionInfoTestPeer;
  static envoy::config::core::v3::ApiVersionNumber makeApiVersion(const char* version);
  static envoy::config::core::v3::ApiVersionNumber
  computeOldestApiVersion(const envoy::config::core::v3::ApiVersionNumber& latest_version);
};

} // namespace Envoy
