#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <vector>

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
 * General representation of a tag.
 */
struct Tag {
  std::string name_;
  std::string value_;
};

class TagExtractor {
public:
  virtual ~TagExtractor() {}

  /**
   * Identifier for this tag.
   */
  virtual std::string name() const PURE;

  /**
   * Updates the tag extracted name and the set of tags by extracting the tag represented by this
   * TagExtractor. If the tag is not represented in the current tag_extracted_name, nothing will be
   * modified.
   * @param[in,out] tag_extracted_name name from which the tag will be removed if found to be
   * represented in the name.
   * @param[in,out] tags list of tags updated with the tag name and value if found in the
   * tag_extracted_name.
   */
  virtual void updateTags(std::string& tag_extracted_name, std::vector<Tag>& tags) const PURE;
};

typedef std::unique_ptr<TagExtractor> TagExtractorPtr;

/**
 * General interface for all stats objects.
 */
class Metric {
public:
  virtual ~Metric() {}
  /**
   * Returns the full name of the Metric.
   */
  virtual std::string name() const PURE;

  /**
   * Returns a vector of configurable tags to identify this Metric.
   */
  virtual const std::vector<Tag>& tags() const PURE;

  /**
   * Returns the name of the Metric with the portions designated as tags removed.
   */
  virtual const std::string& tagExtractedName() const PURE;
};

/**
 * An always incrementing counter with latching capability. Each increment is added both to a
 * global counter as well as periodic counter. Calling latch() returns the periodic counter and
 * clears it.
 */
class Counter : public Metric {
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
class Gauge : public Metric {
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
class Timer : public Metric {
public:
  virtual ~Timer() {}

  virtual TimespanPtr allocateSpan() PURE;
  virtual void recordDuration(std::chrono::milliseconds ms) PURE;
};

typedef std::shared_ptr<Timer> TimerSharedPtr;

/**
 * A histogram that captures values one at a time.
 */
class Histogram : public Metric {
public:
  virtual ~Histogram() {}

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
  virtual void flushCounter(const Metric& counter, uint64_t delta) PURE;

  /**
   * Flush a gauge value.
   */
  virtual void flushGauge(const Metric& gauge, uint64_t value) PURE;

  /**
   * This will be called after beginFlush(), some number of flushCounter(), and some number of
   * flushGauge(). Sinks can use this to optimize writing if desired.
   */
  virtual void endFlush() PURE;

  /**
   * Flush a histogram value.
   */
  virtual void onHistogramComplete(const Metric& histogram, uint64_t value) PURE;

  /**
   * Flush a timespan value.
   */
  virtual void onTimespanComplete(const Metric& timespan, std::chrono::milliseconds ms) PURE;
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
  virtual void deliverHistogramToSinks(const Metric& histogram, uint64_t value) PURE;

  /**
   * Deliver an individual timespan completion to all registered sinks.
   */
  virtual void deliverTimingToSinks(const Metric& timer, std::chrono::milliseconds ms) PURE;

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

  /**
   * @return a histogram within the scope's namespace.
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

typedef std::unique_ptr<Store> StorePtr;

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
   * Add an extractor to extract a portion of stats names as a tag.
   */
  virtual void addTagExtractor(TagExtractor& tag_extractor) PURE;

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
