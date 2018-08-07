#pragma once

#include <cstdint>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/stats/stats.h"

namespace Envoy {
namespace Stats {

/**
 * Holds the computed statistics for a histogram.
 */
class HistogramStatistics {
public:
  virtual ~HistogramStatistics() {}

  /**
   * Returns summary representation of the histogram.
   */
  virtual std::string summary() const PURE;

  /**
   * Returns supported quantiles.
   */
  virtual const std::vector<double>& supportedQuantiles() const PURE;

  /**
   * Returns computed quantile values during the period.
   */
  virtual const std::vector<double>& computedQuantiles() const PURE;
};

/**
 * A histogram that records values one at a time.
 * Note: Histograms now incorporate what used to be timers because the only difference between the
 * two stat types was the units being represented. It is assumed that no downstream user of this
 * class (Sinks, in particular) will need to explicitly differentiate between histograms
 * representing durations and histograms representing other types of data.
 */
class Histogram : public virtual Metric {
public:
  virtual ~Histogram() {}

  /**
   * Records an unsigned value. If a timer, values are in units of milliseconds.
   */
  virtual void recordValue(uint64_t value) PURE;
};

typedef std::shared_ptr<Histogram> HistogramSharedPtr;

/**
 * A histogram that is stored in main thread and provides summary view of the histogram.
 */
class ParentHistogram : public virtual Histogram {
public:
  virtual ~ParentHistogram() {}

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
   * Returns the summary representation.
   */
  virtual const std::string summary() const PURE;
};

typedef std::shared_ptr<ParentHistogram> ParentHistogramSharedPtr;

} // namespace Stats
} // namespace Envoy
