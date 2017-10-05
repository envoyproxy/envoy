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
 * General interface for all stats objects.
 */
class Metric {
public:
  virtual ~Metric() {}
  /**
   * Returns the full name of the Metric.
   */
  virtual const std::string& name() const PURE;
};

/**
 * An always incrementing counter with latching capability. Each increment is added both to a
 * global counter as well as periodic counter. Calling latch() returns the periodic counter and
 * clears it.
 */
class Counter : public virtual Metric {
public:
  virtual ~Counter() {}
  virtual void add(uint64_t amount) PURE;
  virtual void inc() PURE;
  virtual uint64_t latch() PURE;
  virtual void reset() PURE;
  virtual bool used() const PURE;
  virtual uint64_t value() const PURE;
};

typedef std::shared_ptr<Counter> CounterSharedPtr;

/**
 * A gauge that can both increment and decrement.
 */
class Gauge : public virtual Metric {
public:
  virtual ~Gauge() {}

  virtual void add(uint64_t amount) PURE;
  virtual void dec() PURE;
  virtual void inc() PURE;
  virtual void set(uint64_t value) PURE;
  virtual void sub(uint64_t amount) PURE;
  virtual bool used() const PURE;
  virtual uint64_t value() const PURE;
};

typedef std::shared_ptr<Gauge> GaugeSharedPtr;

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
  virtual void flushCounter(const Counter& counter, uint64_t delta) PURE;

  /**
   * Flush a gauge value.
   */
  virtual void flushGauge(const Gauge& gauge, uint64_t value) PURE;

  /**
   * This will be called after beginFlush(), some number of flushCounter(), and some number of
   * flushGauge(). Sinks can use this to optimize writing if desired.
   */
  virtual void endFlush() PURE;

  /**
   * Flush a histogram value.
   */
  virtual void onHistogramComplete(const Histogram& histogram, uint64_t value) PURE;
};

typedef std::unique_ptr<Sink> SinkPtr;

class Scope;
typedef std::unique_ptr<Scope> ScopePtr;

/**
 * A named scope for stats. Scopes are a grouping of stats that can be acted on as a unit if needed
 * (for example to free/delete all of them).
 */
class Scope {
public:
  virtual ~Scope() {}

  /**
   * Allocate a new scope. NOTE: The implementation should correctly handle overlapping scopes
   * that point to the same reference counted backing stats. This allows a new scope to be
   * gracefully swapped in while an old scope with the same name is being destroyed.
   * @param name supplies the scope's namespace prefix.
   */
  virtual ScopePtr createScope(const std::string& name) PURE;

  /**
   * Deliver an individual histogram value to all registered sinks.
   */
  virtual void deliverHistogramToSinks(const Histogram& histogram, uint64_t value) PURE;

  /**
   * @return a counter within the scope's namespace.
   */
  virtual Counter& counter(const std::string& name) PURE;

  /**
   * @return a gauge within the scope's namespace.
   */
  virtual Gauge& gauge(const std::string& name) PURE;

  /**
   * @return a histogram within the scope's namespace with a particular value type.
   */
  virtual Histogram& histogram(const std::string& name) PURE;
};

/**
 * A store for all known counters, gauges, and timers.
 */
class Store : public Scope {
public:
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
