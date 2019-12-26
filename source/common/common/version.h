#pragma once

#include <string>

#include "envoy/api/v2/core/base.pb.h"

#include "common/common/version_number.h"

namespace Envoy {

/**
 * Wraps compiled in code versioning.
 */
class VersionInfo {
public:
  // Repository revision (e.g. git SHA1).
  static const std::string& revision();
  // Repository status (e.g. clean, modified).
  static const std::string& revisionStatus();
  // Repository information and build type.
  static const std::string& version();

  static const envoy::api::v2::core::BuildVersion& buildVersion();

private:
  // RELEASE or DEBUG
  static const std::string& buildType();
  static const std::string& sslVersion();
  static envoy::api::v2::core::BuildVersion makeBuildVersion();
};
} // namespace Envoy
