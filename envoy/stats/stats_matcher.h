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
  // Holds the result of fastRejects(). This contains state that must be then
  // passed to slowRejects() for the final 2-phase determination of whether a
  // stat can be rejected.
  enum class FastResult {
    NoMatch,
    Rejects,
    Matches,
  };

  virtual ~StatsMatcher() = default;

  /**
   * Take a metric name and report whether or not it should be instantiated.
   * This may need to convert the StatName to a string. This is equivalent to
   * calling fastRejects() and then calling slowResults() if necessary.
   *
   * @param name the name of a Stats::Metric.
   * @return bool true if that stat should not be instantiated.
   */
  virtual bool rejects(StatName name) const PURE;

  /**
   * Takes a metric name and quickly determines whether it can be rejected based
   * purely on the StatName. If fastResults(stat_name).rejects() is 'false', we
   * will need to check slowRejects as well. It should not be necessary to cache
   * the result of fastRejects() -- it's cheap enough to recompute. However we
   * should protect slowRejects() by a cache due to its speed and the potential
   * need to take a symbol table lock.
   *
   * @param name the name of a Stats::Metric.
   * @return A result indicating whether the stat can be quickly rejected, as
   *         well as state that is then passed to slowRejects if rejection
   *         cannot be quickly determined.
   */
  virtual FastResult fastRejects(StatName name) const PURE;

  /**
   * Takes a metric name and converts it to a string, if needed, to determine
   * whether it needs to be rejected. This is intended to be used if
   * fastRejects() cannot determine an early rejection. It is a good idea to
   * cache the results of this, to avoid the stringification overhead, potential
   * regex overhead, plus a global symbol table lock.
   *
   * @param fast_result the result of fastRejects(), which must be called first.
   * @param name the name of a Stats::Metric.
   * @return bool true if that stat should not be instantiated.
   */
  virtual bool slowRejects(FastResult fast_result, StatName name) const PURE;

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
};

using StatsMatcherPtr = std::unique_ptr<const StatsMatcher>;

} // namespace Stats
} // namespace Envoy
