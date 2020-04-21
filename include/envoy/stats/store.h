#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_matcher.h"
#include "envoy/stats/tag_producer.h"

namespace Envoy {
namespace Event {

class Dispatcher;
}

namespace ThreadLocal {
class Instance;
}

namespace Stats {

class Sink;

/**
 * A store for all known counters, gauges, and timers.
 */
class Store : public Scope {
public:
  /**
   * @return a list of all known counters.
   */
  virtual std::vector<CounterSharedPtr> counters() const PURE;

  /**
   * @return a list of all known gauges.
   */
  virtual std::vector<GaugeSharedPtr> gauges() const PURE;

  /**
   * @return a list of all known text readouts.
   */
  virtual std::vector<TextReadoutSharedPtr> textReadouts() const PURE;

  /**
   * @return a list of all known histograms.
   */
  virtual std::vector<ParentHistogramSharedPtr> histograms() const PURE;
};

using StorePtr = std::unique_ptr<Store>;

/**
 * Callback invoked when a store's mergeHistogram() runs.
 */
using PostMergeCb = std::function<void()>;

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
   * Set the given tag producer to control tags.
   */
  virtual void setTagProducer(TagProducerPtr&& tag_producer) PURE;

  /**
   * Attach a StatsMatcher to this StoreRoot to prevent the initialization of stats according to
   * some ruleset.
   * @param stats_matcher a StatsMatcher to attach to this StoreRoot.
   */
  virtual void setStatsMatcher(StatsMatcherPtr&& stats_matcher) PURE;

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

  /**
   * Called during the flush process to merge all the thread local histograms. The passed in
   * callback will be called on the main thread, but it will happen after the method returns
   * which means that the actual flush process will happen on the main thread after this method
   * returns. It is expected that only one merge runs at any time and concurrent calls to this
   * method would be asserted.
   */
  virtual void mergeHistograms(PostMergeCb merge_complete_cb) PURE;
};

using StoreRootPtr = std::unique_ptr<StoreRoot>;

} // namespace Stats
} // namespace Envoy
