#pragma once

#include "envoy/common/pure.h"

namespace Stats {

/**
 * An always incrementing counter with latching capability. Each increment is added both to a
 * global counter as well as periodic counter. Calling latch() returns the periodic counter and
 * clears it.
 */
class Counter {
public:
  virtual ~Counter() {}
  virtual void add(uint64_t amount) PURE;
  virtual void inc() PURE;
  virtual uint64_t latch() PURE;
  virtual std::string name() PURE;
  virtual void reset() PURE;
  virtual bool used() PURE;
  virtual uint64_t value() PURE;
};

/**
 * A gauge that can both increment and decrement.
 */
class Gauge {
public:
  virtual ~Gauge() {}

  virtual void add(uint64_t amount) PURE;
  virtual void dec() PURE;
  virtual void inc() PURE;
  virtual std::string name() PURE;
  virtual void set(uint64_t value) PURE;
  virtual void sub(uint64_t amount) PURE;
  virtual bool used() PURE;
  virtual uint64_t value() PURE;
};

/**
 * An individual timespan that is owned by a timer. The initial time is captured on construction.
 * A timespan must be completed via complete() for it to be stored. If the timespan is deleted
 * this will be treated as a cancellation.
 */
class Timespan {
public:
  virtual ~Timespan() {}

  /**
   * Complete the span using the default name of the timer that the span was allocated from.
   */
  virtual void complete() PURE;

  /**
   * Complete the span using a dynamic name. This is useful if a span needs to get counted
   * against a timer with a dynamic name.
   */
  virtual void complete(const std::string& dynamic_name) PURE;
};

typedef std::unique_ptr<Timespan> TimespanPtr;

/**
 * A timer that can capture timespans.
 */
class Timer {
public:
  virtual ~Timer() {}

  virtual TimespanPtr allocateSpan() PURE;
  virtual std::string name() PURE;
};

/**
 * A sink for stats. Each sink is responsible for writing stats to a backing store.
 */
class Sink {
public:
  virtual ~Sink() {}

  /**
   * Flush a counter delta.
   */
  virtual void flushCounter(const std::string& name, uint64_t delta) PURE;

  /**
   * Flush a gauge value.
   */
  virtual void flushGauge(const std::string& name, uint64_t value) PURE;

  /**
   * Flush a histogram value.
   */
  virtual void onHistogramComplete(const std::string& name, uint64_t value) PURE;

  /**
   * Flush a timespan value.
   */
  virtual void onTimespanComplete(const std::string& name, std::chrono::milliseconds ms) PURE;
};

typedef std::unique_ptr<Sink> SinkPtr;

/**
 * A store for all known counters, gauges, and timers.
 */
class Store {
public:
  virtual ~Store() {}

  /**
   * Add a sink that is used for stat flushing.
   */
  virtual void addSink(Sink& sink) PURE;

  /**
   * Deliver an individual histogram value to all registered sinks.
   */
  virtual void deliverHistogramToSinks(const std::string& name, uint64_t value) PURE;

  /**
   * Deliver an individual timespan completion to all registered sinks.
   */
  virtual void deliverTimingToSinks(const std::string& name, std::chrono::milliseconds ms) PURE;

  virtual Counter& counter(const std::string& name) PURE;
  virtual std::list<std::reference_wrapper<Counter>> counters() const PURE;
  virtual Gauge& gauge(const std::string& name) PURE;
  virtual std::list<std::reference_wrapper<Gauge>> gauges() const PURE;
  virtual Timer& timer(const std::string& name) PURE;
};

} // Stats
