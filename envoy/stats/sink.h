#pragma once

#include <cstdint>
#include <memory>

#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/primitive_stats.h"
#include "envoy/stats/stats.h"

namespace Envoy {
namespace Stats {

class Histogram;

class MetricSnapshot {
public:
  struct CounterSnapshot {
    uint64_t delta_;
    std::reference_wrapper<const Counter> counter_;
  };

  virtual ~MetricSnapshot() = default;

  /**
   * @return a snapshot of all counters with pre-latched deltas.
   */
  virtual const std::vector<CounterSnapshot>& counters() PURE;

  /**
   * @return a snapshot of all gauges.
   */
  virtual const std::vector<std::reference_wrapper<const Gauge>>& gauges() PURE;

  /**
   * @return a snapshot of all histograms.
   */
  virtual const std::vector<std::reference_wrapper<const ParentHistogram>>& histograms() PURE;

  /**
   * @return a snapshot of all text readouts.
   */
  virtual const std::vector<std::reference_wrapper<const TextReadout>>& textReadouts() PURE;

  /**
   * @return a snapshot of all host/endpoint-specific primitive counters.
   */
  virtual const std::vector<Stats::PrimitiveCounterSnapshot>& hostCounters() PURE;

  /**
   * @return a snapshot of all host/endpoint-specific primitive gauges.
   */
  virtual const std::vector<Stats::PrimitiveGaugeSnapshot>& hostGauges() PURE;

  /**
   * @return the time in UTC since epoch when the snapshot was created.
   */
  virtual SystemTime snapshotTime() const PURE;
};

/**
 * A class to define predicates to filter counters, gauges and text readouts for flushing to sinks.
 */
class SinkPredicates {
public:
  virtual ~SinkPredicates() = default;

  /**
   * @return true if @param counter needs to be flushed to sinks.
   */
  virtual bool includeCounter(const Counter& counter) PURE;

  /**
   * @return true if @param gauge needs to be flushed to sinks.
   */
  virtual bool includeGauge(const Gauge& gauge) PURE;

  /**
   * @return true if @param text_readout needs to be flushed to sinks.
   */
  virtual bool includeTextReadout(const TextReadout& text_readout) PURE;

  /*
   * @return true if @param histogram needs to be flushed to sinks.
   * Note that this is used only if runtime flag envoy.reloadable_features.enable_include_histograms
   * (which is false by default) is set to true.
   */
  virtual bool includeHistogram(const Histogram& histogram) PURE;
};

/**
 * A sink for stats. Each sink is responsible for writing stats to a backing store.
 */
class Sink {
public:
  virtual ~Sink() = default;

  /**
   * Periodic metric flush to the sink.
   * @param snapshot interface through which the sink can access all metrics being flushed.
   */
  virtual void flush(MetricSnapshot& snapshot) PURE;

  /**
   * Flush a single histogram sample. Note: this call is called synchronously as a part of recording
   * the metric, so implementations must be thread-safe.
   * @param histogram the histogram that this sample applies to.
   * @param value the value of the sample.
   */
  virtual void onHistogramComplete(const Histogram& histogram, uint64_t value) PURE;
};

using SinkPtr = std::unique_ptr<Sink>;

} // namespace Stats
} // namespace Envoy
