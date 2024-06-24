#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/stats/refcount_ptr.h"
#include "envoy/stats/stats.h"

namespace Envoy {
namespace Stats {

using ConstSupportedBuckets = const std::vector<double>;

class HistogramSettings {
public:
  virtual ~HistogramSettings() = default;

  /**
   * For formats like Prometheus where the entire histogram is published (but not
   * like statsd where each value to include in the histogram is emitted separately),
   * get the limits for each histogram bucket.
   * @return The buckets for the histogram. Each value is an upper bound of a bucket.
   */
  virtual ConstSupportedBuckets& buckets(absl::string_view stat_name) const PURE;
};

using HistogramSettingsConstPtr = std::unique_ptr<const HistogramSettings>;

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
  virtual ConstSupportedBuckets& supportedBuckets() const PURE;

  /**
   * Returns computed bucket values during the period. The vector contains an approximation
   * of samples below each quantile bucket defined in supportedBuckets(). This vector is
   * guaranteed to be the same length as supportedBuckets().
   */
  virtual const std::vector<uint64_t>& computedBuckets() const PURE;

  /**
   * Returns version of computedBuckets() with disjoint buckets. This vector is
   * guaranteed to be the same length as supportedBuckets().
   */
  virtual std::vector<uint64_t> computeDisjointBuckets() const PURE;

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

  /**
   * Returns the count of values which are out of the boundaries of the histogram bins.
   * I.e., the count of values in the (bound_of_last_bucket, +inf) bucket.
   */
  virtual uint64_t outOfBoundCount() const PURE;
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
    Percent, // A percent value stored as fixed-point, where the stored value is divided by
             // PercentScale to get the actual value, eg a value of 100% (or 1.0) is encoded as
             // PercentScale, 50% is encoded as PercentScale * 0.5. Encoding as fixed-point allows
             // enough dynamic range, without needing to support floating-point values in
             // histograms.
  };

  // The scaling factor for Unit::Percent.
  static constexpr uint64_t PercentScale = 1000000;

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
  virtual std::string quantileSummary() const PURE;

  /**
   * Returns the bucket summary representation.
   */
  virtual std::string bucketSummary() const PURE;

  // Holds detailed value and counts for a histogram bucket.
  struct Bucket {
    double lower_bound_{0}; // Bound of bucket that's closest to zero.
    double width_{0};
    uint64_t count_{0};
  };

  /**
   * @return a vector of histogram buckets collected since binary start or reset.
   */
  virtual std::vector<Bucket> detailedTotalBuckets() const PURE;

  /**
   * @return bucket data collected since the most recent stat sink. Note that
   *         the number of interval buckets is likely to be much smaller than
   *         the number of detailed buckets.
   */
  virtual std::vector<Bucket> detailedIntervalBuckets() const PURE;
};

using ParentHistogramSharedPtr = RefcountPtr<ParentHistogram>;

} // namespace Stats
} // namespace Envoy
