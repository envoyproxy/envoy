#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
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

  /**
   * Iterate over all stats that need to be added to a sink. Note, that implementations can
   * potentially  hold on to a mutex that will deadlock if the passed in functors try to create
   * or delete a stat.
   * @param f_size functor that is provided the number of all stats in the sink.
   * @param f_stat functor that is provided one stat in the sink at a time.
   */
  virtual void forEachCounter(std::function<void(std::size_t)> f_size,
                              std::function<void(Stats::Counter&)> f_stat) const PURE;

  virtual void forEachGauge(std::function<void(std::size_t)> f_size,
                            std::function<void(Stats::Gauge&)> f_stat) const PURE;

  virtual void forEachTextReadout(std::function<void(std::size_t)> f_size,
                                  std::function<void(Stats::TextReadout&)> f_stat) const PURE;
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
   * Attach a HistogramSettings to this StoreRoot to generate histogram configurations
   * according to some ruleset.
   */
  virtual void setHistogramSettings(HistogramSettingsConstPtr&& histogram_settings) PURE;

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
