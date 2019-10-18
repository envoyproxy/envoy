#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/stats/refcount_ptr.h"
#include "envoy/stats/stats.h"

namespace Envoy {
namespace Stats {

/**
 * Holds the computed statistics for a histogram.
 */
class HistogramStatistics {
public:
  virtual ~HistogramStatistics() = default;

  /**
   * Returns quantile summary representation of the histogram.
   */
  virtual std::string quantileSummary() const PURE;

  /**
   * Returns bucket summary representation of the histogram.
   */
  virtual std::string bucketSummary() const PURE;

  /**
   * Returns supported quantiles.
   */
  virtual const std::vector<double>& supportedQuantiles() const PURE;

  /**
   * Returns computed quantile values during the period.
   */
  virtual const std::vector<double>& computedQuantiles() const PURE;

  /**
   * Returns supported buckets. Each value is the upper bound of the bucket
   * with 0 as the implicit lower bound. For timers, these bucket thresholds
   * are in milliseconds but the thresholds are applicable to all types of data.
   */
  virtual const std::vector<double>& supportedBuckets() const PURE;

  /**
   * Returns computed bucket values during the period. The vector contains an approximation
   * of samples below each quantile bucket defined in supportedBuckets(). This vector is
   * guaranteed to be the same length as supportedBuckets().
   */
  virtual const std::vector<uint64_t>& computedBuckets() const PURE;

  /**
   * Returns number of values during the period. This number may be an approximation
   * of the number of samples in the histogram, it is not guaranteed that this will be
   * 100% the number of samples observed.
   */
  virtual uint64_t sampleCount() const PURE;

  /**
   * Returns sum of all values during the period.
   */
  virtual double sampleSum() const PURE;
};

/**
 * A histogram that records values one at a time.
 * Note: Histograms now incorporate what used to be timers because the only
 * difference between the two stat types was the units being represented.
 */
class Histogram : public Metric {
public:
  /**
   * Histogram values represent scalar quantity like time, length, mass,
   * distance, or in general anything which has only magnitude and no other
   * characteristics. These are often accompanied by a unit of measurement.
   * This enum defines units for commonly measured quantities. Base units
   * are preferred unless they are not granular enough to be useful as an
   * integer.
   */
  enum class Unit {
    Null,        // The histogram has been rejected, i.e. it's a null histogram and is not recording
                 // anything.
    Unspecified, // Measured quantity does not require a unit, e.g. "items".
    Bytes,
    Microseconds,
    Milliseconds,
  };

  ~Histogram() override = default;

  /**
   * @return the unit of measurement for values recorded by the histogram.
   */
  virtual Unit unit() const PURE;

  /**
   * Records an unsigned value in the unit specified during the construction.
   */
  virtual void recordValue(uint64_t value) PURE;
};

using HistogramSharedPtr = RefcountPtr<Histogram>;

/**
 * A histogram that is stored in main thread and provides summary view of the histogram.
 */
class ParentHistogram : public Histogram {
public:
  ~ParentHistogram() override = default;

  /**
   * This method is called during the main stats flush process for each of the histograms and used
   * to merge the histogram values.
   */
  virtual void merge() PURE;

  /**
   * Returns the interval histogram summary statistics for the flush interval.
   */
  virtual const HistogramStatistics& intervalStatistics() const PURE;

  /**
   * Returns the cumulative histogram summary statistics.
   */
  virtual const HistogramStatistics& cumulativeStatistics() const PURE;

  /**
   * Returns the quantile summary representation.
   */
  virtual const std::string quantileSummary() const PURE;

  /**
   * Returns the bucket summary representation.
   */
  virtual const std::string bucketSummary() const PURE;
};

using ParentHistogramSharedPtr = RefcountPtr<ParentHistogram>;

} // namespace Stats
} // namespace Envoy
