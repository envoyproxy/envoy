#pragma once

#include <utility>
#include <vector>

#include "envoy/common/pure.h"

namespace Envoy {

/**
 * Maintains sets of numeric intervals. As new intervals are added, existing ones in the
 * set are combined so that no overlapping intervals remain in the representation.
 *
 * Value can be any type that is comparable with <, ==, and >.
 */
template <typename Value> class IntervalSet {
public:
  virtual ~IntervalSet() {}

  typedef std::pair<Value, Value> Interval;

  /**
   * Inserts a new interval into the set, merging any overlaps. The intervals are in
   * the form [left_inclusive, right_exclusive). E.g. an interval [3, 5) includes the
   * numbers 3 and 4, but not 5.
   * @param left_inclusive Value the left-bound, inclusive.
   * @param right_exclusive Value the right-bound, which is exclusive.
   */
  virtual void insert(Value left_inclusive, Value right_exclusive) PURE;

  /**
   * @return std::vector<Interval> the interval-set as a vector.
   */
  virtual std::vector<Interval> toVector() const PURE;

  /**
   * Clears the contents of the interval set.
   */
  virtual void clear() PURE;
};

} // namespace Envoy
