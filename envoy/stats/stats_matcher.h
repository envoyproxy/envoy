#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Stats {

class StatName;

class StatsMatcher {
public:
  virtual ~StatsMatcher() = default;

  /**
   * Take a metric name and report whether or not it should be instantiated.
   * The may need to convert the StatName to a string.
   *
   * @param the name of a Stats::Metric.
   * @return bool true if that stat should not be instantiated.
   */
  virtual bool rejects(StatName name) const PURE;

  /**
   * Takes a metric name and quickly determine whether it can be rejected based
   * purely on the StatName. A return of 'false' means we may need to check
   * slowRejects as well.
   *
   * @param the name of a Stats::Metric.
   * @return bool true if that stat should not be instantiated, or whether we
   *                   need to check slowRejects.
   */
  virtual bool fastRejects(StatName name) const PURE;

  /**
   * Takes a metric name and quickly determine whether it can be rejected based
   * purely on the StatName. A return of 'false' means we may need to check
   * slowRejects as well.
   *
   * @param the name of a Stats::Metric.
   * @return bool true if that stat should not be instantiated, or whether we
   *                   need to check slowRejects.
   */
  virtual bool slowRejects(StatName name) const PURE;

  /**
   * Helps determine whether the matcher needs to be called. This can be used
   * to short-circuit elaboration of stats names.
   *
   * @return bool whether StatsMatcher can be statically determined to accept
   *              all stats. It's possible to construct a matcher where
   *              acceptsAll() returns false, but rejects() is always false.
   */
  virtual bool acceptsAll() const PURE;

  /**
   * Helps determine whether the matcher needs to be called. This can be used
   * to short-circuit elaboration of stats names.
   *
   * @return bool whether StatsMatcher can be statically determined to reject
   *              all stats. It's possible to construct a matcher where
   *              rejectsAll() returns false, but rejects() is always true.
   */
  virtual bool rejectsAll() const PURE;

  // Determines whether conversion from StatName to string may be necessary to
  // run a match against this set.
  virtual bool hasStringMatchers() const PURE;
};

using StatsMatcherPtr = std::unique_ptr<const StatsMatcher>;

} // namespace Stats
} // namespace Envoy
