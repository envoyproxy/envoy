#pragma once

#include <string>

namespace Envoy {
namespace Stats {

/**
 * Common stats utility routines.
 */
class Utility {
public:
  // ':' is a reserved char in statsd. Do a character replacement to avoid costly inline
  // translations later.
  static std::string sanitizeStatsName(const std::string& name);
};

} // namespace Stats
} // namespace Envoy
