#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/stats/histogram.h"
#include "envoy/stats/stats.h"

namespace Envoy {
namespace Stats {

/**
 * Provides cached access to a particular store's stats.
 */
class Source {
public:
  virtual ~Source() {}

  /**
   * Returns all known counters. Will use cached values if already accessed and clearCache() hasn't
   * been called since.
   * @return std::vector<CounterSharedPtr>& all known counters. Note: reference may not be valid
   * after clearCache() is called.
   */
  virtual const std::vector<CounterSharedPtr>& cachedCounters() PURE;

  /**
   * Returns all known gauges. Will use cached values if already accessed and clearCache() hasn't
   * been called since.
   * @return std::vector<GaugeSharedPtr>& all known gauges. Note: reference may not be valid after
   * clearCache() is called.
   */
  virtual const std::vector<GaugeSharedPtr>& cachedGauges() PURE;

  /**
   * Returns all known parent histograms. Will use cached values if already accessed and
   * clearCache() hasn't been called since.
   * @return std::vector<ParentHistogramSharedPtr>& all known histograms. Note: reference may not be
   * valid after clearCache() is called.
   */
  virtual const std::vector<ParentHistogramSharedPtr>& cachedHistograms() PURE;

  /**
   * Resets the cache so that any future calls to get cached metrics will refresh the set.
   */
  virtual void clearCache() PURE;
};

} // namespace Stats
} // namespace Envoy
