#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Event {
class Dispatcher;
}

namespace ThreadLocal {
class Instance;
}

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

typedef std::shared_ptr<Counter> CounterSharedPtr;

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

typedef std::shared_ptr<Gauge> GaugeSharedPtr;

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

typedef std::shared_ptr<Timer> TimerSharedPtr;

/**
 * A sink for stats. Each sink is responsible for writing stats to a backing store.
 */
class Sink {
public:
  virtual ~Sink() {}

  /**
   * This will be called before a sequence of flushCounter() and flushGauge() calls. Sinks can
   * choose to optimize writing if desired with a paired endFlush() call.
   */
  virtual void beginFlush() PURE;

  /**
   * Flush a counter delta.
   */
  virtual void flushCounter(const std::string& name, uint64_t delta) PURE;

  /**
   * Flush a gauge value.
   */
  virtual void flushGauge(const std::string& name, uint64_t value) PURE;

  /**
   * This will be called after beginFlush(), some number of flushCounter(), and some number of
   * flushGauge(). Sinks can use this to optimize writing if desired.
   */
  virtual void endFlush() PURE;

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
 * A named scope for stats. Scopes are a grouping of stats that can be acted on as a unit if needed
 * (for example to free/delete all of them).
 */
class Scope {
public:
  virtual ~Scope() {}

  /**
   * Deliver an individual histogram value to all registered sinks.
   */
  virtual void deliverHistogramToSinks(const std::string& name, uint64_t value) PURE;

  /**
   * Deliver an individual timespan completion to all registered sinks.
   */
  virtual void deliverTimingToSinks(const std::string& name, std::chrono::milliseconds ms) PURE;

  /**
   * @return a counter within the scope's namespace.
   */
  virtual Counter& counter(const std::string& name) PURE;

  /**
   * @return a gauge within the scope's namespace.
   */
  virtual Gauge& gauge(const std::string& name) PURE;

  /**
   * @return a timer within the scope's namespace.
   */
  virtual Timer& timer(const std::string& name) PURE;
};

typedef std::unique_ptr<Scope> ScopePtr;

/**
 * A store for all known counters, gauges, and timers.
 */
class Store : public Scope {
public:
  /**
   * Allocate a new scope. NOTE: The implementation should correctly handle overlapping scopes
   * that point to the same reference counted backing stats. This allows a new scope to be
   * gracefully swapped in while an old scope with the same name is being destroyed.
   * @param name supplies the scope's namespace prefix.
   */
  virtual ScopePtr createScope(const std::string& name) PURE;

  /**
   * @return a list of all known counters.
   */
  virtual std::list<CounterSharedPtr> counters() const PURE;

  /**
   * @return a list of all known gauges.
   */
  virtual std::list<GaugeSharedPtr> gauges() const PURE;
};

/**
 * The root of the stat store.
 */
class StoreRoot : public Store {
public:
  /**
   * Add a sink that is used for stat flushing.
   */
  virtual void addSink(Sink& sink) PURE;

  /**
   * Initialize the store for threading. This will be called once after all worker threads have
   * been initialized. At this point the store can initialize itself for multi-threaded operation.
   */
  virtual void initializeThreading(Event::Dispatcher& main_thread_dispatcher,
                                   ThreadLocal::Instance& tls) PURE;

  /**
   * Shutdown threading support in the store. This is called once when the server is about to shut
   * down.
   */
  virtual void shutdownThreading() PURE;
};

typedef std::unique_ptr<StoreRoot> StoreRootPtr;

} // namespace Stats
} // namespace Envoy
