#pragma once

#include <string>

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

/**
 * Common stats utility routines.
 */
class Utility {
public:
  // ':' is a reserved char in statsd. Do a character replacement to avoid costly inline
  // translations later.
  static std::string sanitizeStatsName(absl::string_view name);
};

} // namespace Stats
} // namespace Envoy
