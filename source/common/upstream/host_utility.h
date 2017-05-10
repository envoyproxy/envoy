#pragma once

#include <string>

#include "envoy/upstream/upstream.h"

namespace Lyft {
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

} // Upstream
} // Lyft