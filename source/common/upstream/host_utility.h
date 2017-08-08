#pragma once

#include <string>

#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Upstream {

/**
 * Utility functions for hosts.
 */
class HostUtility {
public:
  /**
   * Convert a host's health flags into a debug string.
   */
  static std::string healthFlagsToString(const Host& host);
};

} // namespace Upstream
} // namespace Envoy
