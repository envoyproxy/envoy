#pragma once

#include <utility>
#include <vector>

#include "envoy/common/pure.h"

namespace Envoy {

/**
 * Maintains sets of numeric intervals. As new intervals are added, existing ones in the
 * set are combined so that no overlapping intervals remain in the representation.
 *
 * Value can be any type that is comparable with <.
 */
template <typename Value> class IntervalSet {
public:
  virtual ~IntervalSet() {}

  typedef std::pair<Value, Value> Interval;

  // Inserts a new interval into the set, merging any overlaps.
  virtual void insert(Value left, Value right) PURE;

  // Returns the interval-set as a vector.
  virtual std::vector<Interval> toVector() const PURE;
};

} // namespace Envoy
