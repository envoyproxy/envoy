#pragma once

#include <string>

#include "envoy/stats/histogram.h"
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

  /**
   * Returns the stat name suffixed with the unit symbol (unless Unspecified) separated by an
   * underscore, e.g. ("duration", Milliseconds) -> "duration_ms" but ("items", Unspecified) ->
   * "items".
   *
   * @param name The stat name to suffix.
   * @param unit The unit of measurement for the stat name specified.
   * @return The suffixed stat name if the unit is specified, the stat name otherwise.
   */
  static std::string suffixedStatsName(absl::string_view name, Histogram::Unit unit);

  /**
   * Returns the ASCII SI symbol for the unit with the metric prefix if applicable,
   * e.g. base unit for time is a second whose symbol is "s", but in case of a non-base unit
   * like millisecond, the metric prefix is used and so "ms" is returned.
   *
   * @param unit The unit of measurement.
   * @return The ASCII SI symbol for the unit.
   */
  static absl::string_view unitSymbol(Histogram::Unit unit);
};

} // namespace Stats
} // namespace Envoy
