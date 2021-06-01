#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Stats {

class StatsMatcher {
public:
  virtual ~StatsMatcher() = default;

  /**
   * Take a metric name and report whether or not it should be instantiated.
   * @param the name of a Stats::Metric.
   * @return bool true if that stat should not be instantiated.
   */
  virtual bool rejects(const std::string& name) const PURE;

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
