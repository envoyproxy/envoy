#pragma once

#include <string>

#include "envoy/stats/stats.h"

#include "common/stats/symbol_table_impl.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Stats {

/**
 * Common stats utility routines.
 */
class Utility {
public:
  /**
   * ':' is a reserved char in statsd. Do a character replacement to avoid
   * costly inline translations later.
   *
   * @param name the stat name to sanitize.
   * @return the sanitized stat name.
   */
  static std::string sanitizeStatsName(absl::string_view name);

  /**
   * Finds a metric tag with the specified name.
   *
   * @param metric The metric in which the tag is expected to exist.
   * @param find_tag_name The name of the tag to search for.
   * @return The value of the tag, if found.
   */
  static absl::optional<StatName> findTag(const Metric& metric, StatName find_tag_name);
};

} // namespace Stats
} // namespace Envoy
