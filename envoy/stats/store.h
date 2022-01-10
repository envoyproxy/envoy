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
class SinkPredicates;

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
   * Iterate over all stats. Note, that implementations can potentially hold on to a mutex that
   * will deadlock if the passed in functors try to create or delete a stat.
   * @param f_size functor that is provided the current number of all stats. Note that this is
   * called only once, prior to any calls to f_stat.
   * @param f_stat functor that is provided one stat at a time from the stats container.
   */
  virtual void forEachCounter(SizeFn f_size, StatFn<Counter> f_stat) const PURE;
  virtual void forEachGauge(SizeFn f_size, StatFn<Gauge> f_stat) const PURE;
  virtual void forEachTextReadout(SizeFn f_size, StatFn<TextReadout> f_stat) const PURE;

  /**
   * Iterate over all stats that need to be flushed to sinks. Note, that implementations can
   * potentially hold on to a mutex that will deadlock if the passed in functors try to create
   * or delete a stat.
   * @param f_size functor that is provided the number of all stats that will be flushed to sinks.
   * Note that this is called only once, prior to any calls to f_stat.
   * @param f_stat functor that is provided one stat that will be flushed to sinks, at a time.
   */
  virtual void forEachSinkedCounter(SizeFn f_size, StatFn<Counter> f_stat) const PURE;
  virtual void forEachSinkedGauge(SizeFn f_size, StatFn<Gauge> f_stat) const PURE;
  virtual void forEachSinkedTextReadout(SizeFn f_size, StatFn<TextReadout> f_stat) const PURE;
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

  /**
   * Set predicates for filtering counters, gauges and text readouts to be flushed to sinks.
   * Note that if the sink predicates object is set, we do not send non-sink stats over to the
   * child process during hot restart. This will result in the admin stats console being wrong
   * during hot restart.
   */
  virtual void setSinkPredicates(std::unique_ptr<SinkPredicates>&& sink_predicates) PURE;
};

using StoreRootPtr = std::unique_ptr<StoreRoot>;

} // namespace Stats
} // namespace Envoy
