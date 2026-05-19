#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/stats/stats.h"

#include "gmock/gmock.h"

namespace Envoy {

// Abstract interface for IntegrationTestServer stats methods.
class IntegrationTestServerStats {
public:
  virtual ~IntegrationTestServerStats() = default;

  /**
   * Wait for a counter to match a given value matcher.
   * @param name counter name.
   * @param value_matcher supplies the value matcher for the counter.
   * @param timeout amount of time to wait before asserting false, or 0 for no timeout.
   * @param dispatcher the dispatcher to run non-blocking periodically during the wait.
   */
  virtual void waitForCounter(const std::string& name, testing::Matcher<uint64_t> value_matcher,
                              std::chrono::milliseconds timeout = std::chrono::milliseconds::zero(),
                              Event::Dispatcher* dispatcher = nullptr) PURE;

  /**
   * Wait for a counter to exist.
   * @param name counter name.
   */
  virtual void waitForCounterExists(const std::string& name) PURE;

  /**
   * Wait for a counter to not exist.
   * @param name counter name.
   * @param timeout amount of time to wait for the counter to not exist.
   */
  virtual void waitForCounterNonexistent(const std::string& name,
                                         std::chrono::milliseconds timeout) PURE;

  /**
   * Wait until a histogram has samples.
   * @param name histogram name.
   */
  virtual void waitUntilHistogramHasSamples(
      const std::string& name,
      std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) PURE;

  /**
   * Wait until a histogram has at least sample_count samples.
   * @param name histogram name.
   * @param sample_count the number of samples the histogram should at least
   * have.
   */
  virtual void waitForNumHistogramSamplesGe(
      const std::string& name, uint64_t sample_count,
      std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) PURE;

  /**
   * Wait for a gauge to match a given value matcher.
   * @param name gauge name.
   * @param value_matcher supplies the value matcher for the gauge.
   * @param timeout amount of time to wait before asserting false, or 0 for no timeout.
   */
  virtual void
  waitForGauge(const std::string& name, testing::Matcher<uint64_t> value_matcher,
               std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) PURE;

  /**
   * Wait for a gauge to be destroyed. Note that MockStatStore does not destroy stat.
   * @param name gauge name.
   */
  virtual void waitForGaugeDestroyed(const std::string& name) PURE;

  /**
   * Counter lookup. This is not thread safe, since we don't get a consistent
   * snapshot, uses counters() instead for this behavior.
   * @param name counter name.
   * @return Stats::CounterSharedPtr counter if it exists, otherwise nullptr.
   */
  virtual Stats::CounterSharedPtr counter(const std::string& name) PURE;

  /**
   * Gauge lookup. This is not thread safe, since we don't get a consistent
   * snapshot, uses gauges() instead for this behavior.
   * @param name gauge name.
   * @return Stats::GaugeSharedPtr gauge if it exists, otherwise nullptr.
   */
  virtual Stats::GaugeSharedPtr gauge(const std::string& name) PURE;

  /**
   * @return std::vector<Stats::CounterSharedPtr> snapshot of server counters.
   */
  virtual std::vector<Stats::CounterSharedPtr> counters() PURE;

  /**
   * @return std::vector<Stats::GaugeSharedPtr> snapshot of server counters.
   */
  virtual std::vector<Stats::GaugeSharedPtr> gauges() PURE;

  /**
   * @return std::vector<Stats::ParentHistogramSharedPtr> snapshot of server histograms.
   */
  virtual std::vector<Stats::ParentHistogramSharedPtr> histograms() PURE;
};

} // namespace Envoy
