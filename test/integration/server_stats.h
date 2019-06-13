#pragma once

#include "envoy/stats/stats.h"

namespace Envoy {

// Abstract interface for IntegrationTestServer stats methods.
class IntegrationTestServerStats {
public:
  virtual ~IntegrationTestServerStats() = default;

  /**
   * Wait for a counter to == a given value.
   * @param name counter name.
   * @param value target value.
   */
  virtual void waitForCounterEq(const std::string& name, uint64_t value) PURE;

  /**
   * Wait for a counter to >= a given value.
   * @param name counter name.
   * @param value target value.
   */
  virtual void waitForCounterGe(const std::string& name, uint64_t value) PURE;

  /**
   * Wait for a gauge to >= a given value.
   * @param name gauge name.
   * @param value target value.
   */
  virtual void waitForGaugeGe(const std::string& name, uint64_t value) PURE;

  /**
   * Wait for a gauge to == a given value.
   * @param name gauge name.
   * @param value target value.
   */
  virtual void waitForGaugeEq(const std::string& name, uint64_t value) PURE;

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
};

} // namespace Envoy
