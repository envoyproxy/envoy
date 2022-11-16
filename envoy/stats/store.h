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
class Store {
public:
  virtual ~Store() = default;

  virtual ScopeSharedPtr rootScope() PURE;
  virtual ConstScopeSharedPtr constRootScope() const PURE;
  virtual const SymbolTable& constSymbolTable() const PURE;
  virtual SymbolTable& symbolTable() PURE;

  /**
   * Deliver an individual histogram value to all registered sinks.
   */
  virtual void deliverHistogramToSinks(const Histogram& histogram, uint64_t value) PURE;

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
   * Iterate over all stats. Note, that implementations can potentially hold on
   * to a mutex that will deadlock if the passed in functors try to create or
   * delete a stat. Also note that holding onto the stat or scope reference
   * after forEach* is not supported, as scope/stat deletions can occur in any
   * thread. Implementation locks ensures the stat/scope is valid until the
   * f_stat returns.
   *
   * @param f_size functor that is provided the current number of all
   * stats. Note that this is called only once, prior to any calls to f_stat.
   * @param f_stat functor that is provided one stat at a time from the stats
   * container.
   */
  virtual void forEachCounter(SizeFn f_size, StatFn<Counter> f_stat) const PURE;
  virtual void forEachGauge(SizeFn f_size, StatFn<Gauge> f_stat) const PURE;
  virtual void forEachTextReadout(SizeFn f_size, StatFn<TextReadout> f_stat) const PURE;
  virtual void forEachHistogram(SizeFn f_size, StatFn<ParentHistogram> f_stat) const PURE;
  virtual void forEachScope(SizeFn f_size, StatFn<const Scope> f_stat) const PURE;

  /**
   * @param The name of the stat, obtained from the SymbolTable.
   * @return a reference to a counter within the scope's namespace, if it exists.
   */
  virtual CounterOptConstRef findCounter(StatName name) const PURE;

  /**
   * @param The name of the stat, obtained from the SymbolTable.
   * @return a reference to a gauge within the scope's namespace, if it exists.
   */
  virtual GaugeOptConstRef findGauge(StatName name) const PURE;

  /**
   * @param The name of the stat, obtained from the SymbolTable.
   * @return a reference to a histogram within the scope's namespace, if it
   * exists.
   */
  virtual HistogramOptConstRef findHistogram(StatName name) const PURE;

  /**
   * @param The name of the stat, obtained from the SymbolTable.
   * @return a reference to a text readout within the scope's namespace, if it exists.
   */
  virtual TextReadoutOptConstRef findTextReadout(StatName name) const PURE;

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

  /**
   * @return a null gauge within the scope's namespace.
   */
  virtual Gauge& nullGauge() PURE;
  virtual Counter& nullCounter() PURE;

  virtual bool iterate(const IterateFn<Counter>& fn) const PURE;
  virtual bool iterate(const IterateFn<Gauge>& fn) const PURE;
  virtual bool iterate(const IterateFn<Histogram>& fn) const PURE;
  virtual bool iterate(const IterateFn<TextReadout>& fn) const PURE;


  // TODO(#24007): The cast operator is available temporarily to bound the size
  // of https://github.com/envoyproxy/envoy/pull/23851, which detaches the
  // inheritance of Scope as a parent of Store. There is semantic complexity to
  // that PR, so it's going to be easier review if it's as small as possible.
  //
  // A follow-up PR is required to remove the functions below, which will
  // require a large number of files to be trivially changed, by explicitly
  // accessing the rootScope() to call these methods.
  operator Scope&() { return *rootScope(); }

  // Delegate somea methods to the root scope; these are exposed to make it more
  // convenient to use stats_macros.h. We can consider dropping them if desired,
  // when we resovle #24007 or in the next follow-up.
  Counter& counterFromString(const std::string& name) {
    return rootScope()->counterFromString(name);
  }
  Gauge& gaugeFromString(const std::string& name, Gauge::ImportMode import_mode) {
    return rootScope()->gaugeFromString(name, import_mode);
  }
  TextReadout& textReadoutFromString(const std::string& name) {
    return rootScope()->textReadoutFromString(name);
  }
  Histogram& histogramFromString(const std::string& name, Histogram::Unit unit) {
    return rootScope()->histogramFromString(name, unit);
  }
  ScopeSharedPtr createScope(const std::string& name) { return rootScope()->createScope(name); }
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
