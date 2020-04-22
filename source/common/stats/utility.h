#pragma once

#include <string>

#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"

#include "common/stats/symbol_table_impl.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Stats {

/**
 * Represents a stat name token, using either a StatName or a string_view,
 * which will be treated as a dynamic string. We subclass string_view simply
 * to make it a bit more explicit when we are creating a dynamic stat name,
 * since those are expensive.
 */
class DynamicName : public absl::string_view {
public:
  DynamicName(absl::string_view s) : absl::string_view(s) {}
};

using Element = absl::variant<StatName, DynamicName>;
using ElementVec = std::vector<Element>;

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
   * Creates a counter from a vector of tokens which are used to create the
   * name. The tokens can be specified as string_view or StatName. For
   * tokens specified as string_view, a dynamic StatName will be created. See
   * https://github.com/envoyproxy/envoy/blob/master/source/docs/stats.md#dynamic-stat-tokens
   * for more detail on why symbolic StatNames are preferred when possible.
   *
   * @param scope The scope in which to create the counter.
   * @param elements The vector of mixed string_view and StatName
   * @param tags optionally specified tags.
   * @return A counter named using the joined elements.
   */
  static Counter& counterFromElements(Scope& scope, const ElementVec& elements,
                                      StatNameTagVectorOptConstRef tags = absl::nullopt);

  /**
   * Creates a gauge from a vector of tokens which are used to create the
   * name. The tokens can be specified as string_view or StatName. For
   * tokens specified as string_view, a dynamic StatName will be created. See
   * https://github.com/envoyproxy/envoy/blob/master/source/docs/stats.md#dynamic-stat-tokens
   * for more detail on why symbolic StatNames are preferred when possible.
   *
   * @param scope The scope in which to create the counter.
   * @param elements The vector of mixed string_view and StatName
   * @param import_mode Whether hot-restart should accumulate this value.
   * @param tags optionally specified tags.
   * @return A gauge named using the joined elements.
   */
  static Gauge& gaugeFromElements(Scope& scope, const ElementVec& elements,
                                  Gauge::ImportMode import_mode,
                                  StatNameTagVectorOptConstRef tags = absl::nullopt);

  /**
   * Creates a histogram from a vector of tokens which are used to create the
   * name. The tokens can be specified as string_view or StatName. For
   * tokens specified as string_view, a dynamic StatName will be created. See
   * https://github.com/envoyproxy/envoy/blob/master/source/docs/stats.md#dynamic-stat-tokens
   * for more detail on why symbolic StatNames are preferred when possible.
   *
   * @param scope The scope in which to create the counter.
   * @param elements The vector of mixed string_view and StatName
   * @param unit The unit of measurement.
   * @param tags optionally specified tags.
   * @return A histogram named using the joined elements.
   */
  static Histogram& histogramFromElements(Scope& scope, const ElementVec& elements,
                                          Histogram::Unit unit,
                                          StatNameTagVectorOptConstRef tags = absl::nullopt);
};

} // namespace Stats
} // namespace Envoy
