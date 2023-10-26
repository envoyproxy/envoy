#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/stats/tag.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/hash.h"
#include "source/common/common/thread_synchronizer.h"
#include "source/common/stats/allocator_impl.h"
#include "source/common/stats/histogram_impl.h"
#include "source/common/stats/null_counter.h"
#include "source/common/stats/null_gauge.h"
#include "source/common/stats/null_text_readout.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/utility.h"

#include "absl/container/flat_hash_map.h"
#include "circllhist.h"

namespace Envoy {
namespace Stats {

/**
 * A histogram that is stored in TLS and used to record values per thread. This holds two
 * histograms, one to collect the values and other as backup that is used for merge process. The
 * swap happens during the merge process.
 */
class ThreadLocalHistogramImpl : public HistogramImplHelper {
public:
  ThreadLocalHistogramImpl(StatName name, Histogram::Unit unit, StatName tag_extracted_name,
                           const StatNameTagVector& stat_name_tags, SymbolTable& symbol_table);
  ~ThreadLocalHistogramImpl() override;

  void merge(histogram_t* target);

  /**
   * Called in the beginning of merge process. Swaps the histogram used for collection so that we do
   * not have to lock the histogram in high throughput TLS writes.
   */
  void beginMerge() {
    // This switches the current_active_ between 1 and 0.
    ASSERT(std::this_thread::get_id() == created_thread_id_);
    current_active_ = otherHistogramIndex();
  }

  // Stats::Histogram
  Histogram::Unit unit() const override {
    // If at some point ThreadLocalHistogramImpl will hold a pointer to its parent we can just
    // return parent's unit here and not store it separately.
    return unit_;
  }
  void recordValue(uint64_t value) override;

  // Stats::Metric
  SymbolTable& symbolTable() final { return symbol_table_; }
  bool used() const override { return used_; }
  bool hidden() const override { return false; }

private:
  Histogram::Unit unit_;
  uint64_t otherHistogramIndex() const { return 1 - current_active_; }
  uint64_t current_active_{0};
  histogram_t* histograms_[2];
  std::atomic<bool> used_;
  std::thread::id created_thread_id_;
  SymbolTable& symbol_table_;
};

using TlsHistogramSharedPtr = RefcountPtr<ThreadLocalHistogramImpl>;

class ThreadLocalStoreImpl;

/**
 * Log Linear Histogram implementation that is stored in the main thread.
 */
class ParentHistogramImpl : public MetricImpl<ParentHistogram> {
public:
  ParentHistogramImpl(StatName name, Histogram::Unit unit, ThreadLocalStoreImpl& parent,
                      StatName tag_extracted_name, const StatNameTagVector& stat_name_tags,
                      ConstSupportedBuckets& supported_buckets, uint64_t id);
  ~ParentHistogramImpl() override;

  void addTlsHistogram(const TlsHistogramSharedPtr& hist_ptr);

  // Stats::Histogram
  Histogram::Unit unit() const override;
  void recordValue(uint64_t value) override;

  /**
   * This method is called during the main stats flush process for each of the histograms. It
   * iterates through the TLS histograms and collects the histogram data of all of them
   * in to "interval_histogram". Then the collected "interval_histogram" is merged to a
   * "cumulative_histogram".
   */
  void merge() override;

  const HistogramStatistics& intervalStatistics() const override { return interval_statistics_; }
  const HistogramStatistics& cumulativeStatistics() const override {
    return cumulative_statistics_;
  }
  std::string quantileSummary() const override;
  std::string bucketSummary() const override;
  std::vector<Bucket> detailedTotalBuckets() const override {
    return detailedlBucketsHelper(*cumulative_histogram_);
  }
  std::vector<Bucket> detailedIntervalBuckets() const override {
    return detailedlBucketsHelper(*interval_histogram_);
  }

  // Stats::Metric
  SymbolTable& symbolTable() override;
  bool used() const override;
  bool hidden() const override;

  // RefcountInterface
  void incRefCount() override;
  bool decRefCount() override;
  uint32_t use_count() const override { return ref_count_; }

  // Indicates that the ThreadLocalStore is shutting down, so no need to clear its histogram_set_.
  void setShuttingDown(bool shutting_down) { shutting_down_ = shutting_down; }
  bool shuttingDown() const { return shutting_down_; }

private:
  bool usedLockHeld() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(merge_lock_);
  static std::vector<Stats::ParentHistogram::Bucket>
  detailedlBucketsHelper(const histogram_t& histogram);

  Histogram::Unit unit_;
  ThreadLocalStoreImpl& thread_local_store_;
  histogram_t* interval_histogram_;
  histogram_t* cumulative_histogram_;
  HistogramStatisticsImpl interval_statistics_;
  HistogramStatisticsImpl cumulative_statistics_;
  mutable Thread::MutexBasicLockable merge_lock_;
  std::list<TlsHistogramSharedPtr> tls_histograms_ ABSL_GUARDED_BY(merge_lock_);
  bool merged_{false};
  std::atomic<bool> shutting_down_{false};
  std::atomic<uint32_t> ref_count_{0};
  const uint64_t id_; // Index into TlsCache::histogram_cache_.
};

using ParentHistogramImplSharedPtr = RefcountPtr<ParentHistogramImpl>;

/**
 * Store implementation with thread local caching. For design details see
 * https://github.com/envoyproxy/envoy/blob/main/source/docs/stats.md
 */
class ThreadLocalStoreImpl : Logger::Loggable<Logger::Id::stats>, public StoreRoot {
public:
  static const char DeleteScopeSync[];
  static const char IterateScopeSync[];
  static const char MainDispatcherCleanupSync[];

  ThreadLocalStoreImpl(Allocator& alloc);
  ~ThreadLocalStoreImpl() override;
  // Stats::Store
  NullCounterImpl& nullCounter() override { return null_counter_; }
  NullGaugeImpl& nullGauge() override { return null_gauge_; }
  ScopeSharedPtr rootScope() override { return default_scope_; }
  ConstScopeSharedPtr constRootScope() const override { return default_scope_; }
  const SymbolTable& constSymbolTable() const override { return alloc_.constSymbolTable(); }
  SymbolTable& symbolTable() override { return alloc_.symbolTable(); }

  bool iterate(const IterateFn<Counter>& fn) const override { return iterHelper(fn); }
  bool iterate(const IterateFn<Gauge>& fn) const override { return iterHelper(fn); }
  bool iterate(const IterateFn<Histogram>& fn) const override { return iterHelper(fn); }
  bool iterate(const IterateFn<TextReadout>& fn) const override { return iterHelper(fn); }

  std::vector<CounterSharedPtr> counters() const override;
  std::vector<GaugeSharedPtr> gauges() const override;
  std::vector<TextReadoutSharedPtr> textReadouts() const override;
  std::vector<ParentHistogramSharedPtr> histograms() const override;

  void forEachCounter(SizeFn f_size, StatFn<Counter> f_stat) const override;
  void forEachGauge(SizeFn f_size, StatFn<Gauge> f_stat) const override;
  void forEachTextReadout(SizeFn f_size, StatFn<TextReadout> f_stat) const override;
  void forEachHistogram(SizeFn f_size, StatFn<ParentHistogram> f_stat) const override;
  void forEachScope(SizeFn f_size, StatFn<const Scope> f_stat) const override;

  // Stats::StoreRoot
  void addSink(Sink& sink) override { timer_sinks_.push_back(sink); }
  void setTagProducer(TagProducerPtr&& tag_producer) override {
    tag_producer_ = std::move(tag_producer);
  }
  void setStatsMatcher(StatsMatcherPtr&& stats_matcher) override;
  void setHistogramSettings(HistogramSettingsConstPtr&& histogram_settings) override;
  void initializeThreading(Event::Dispatcher& main_thread_dispatcher,
                           ThreadLocal::Instance& tls) override;
  void shutdownThreading() override;
  void mergeHistograms(PostMergeCb merge_cb) override;
  void deliverHistogramToSinks(const Histogram& histogram, uint64_t value) override;

  Histogram& tlsHistogram(ParentHistogramImpl& parent, uint64_t id);

  void forEachSinkedCounter(SizeFn f_size, StatFn<Counter> f_stat) const override;
  void forEachSinkedGauge(SizeFn f_size, StatFn<Gauge> f_stat) const override;
  void forEachSinkedTextReadout(SizeFn f_size, StatFn<TextReadout> f_stat) const override;
  void forEachSinkedHistogram(SizeFn f_size, StatFn<ParentHistogram> f_stat) const override;

  void setSinkPredicates(std::unique_ptr<SinkPredicates>&& sink_predicates) override;
  OptRef<SinkPredicates> sinkPredicates() override { return sink_predicates_; }

  /**
   * @return a thread synchronizer object used for controlling thread behavior in tests.
   */
  Thread::ThreadSynchronizer& sync() { return sync_; }

  /**
   * @return a set of well known tag names; used to reduce symbol table churn.
   */
  const StatNameSet& wellKnownTags() const { return *well_known_tags_; }

  bool decHistogramRefCount(ParentHistogramImpl& histogram, std::atomic<uint32_t>& ref_count);
  void releaseHistogramCrossThread(uint64_t histogram_id);

  const TagProducer& tagProducer() const { return *tag_producer_; }
  void extractAndAppendTags(StatName name, StatNamePool& pool, StatNameTagVector& tags) override;
  void extractAndAppendTags(absl::string_view name, StatNamePool& pool,
                            StatNameTagVector& tags) override;
  const TagVector& fixedTags() override { return tag_producer_->fixedTags(); };

private:
  friend class ThreadLocalStoreTestingPeer;

  template <class Stat> using StatRefMap = StatNameHashMap<std::reference_wrapper<Stat>>;

  struct TlsCacheEntry {
    // The counters, gauges and text readouts in the TLS cache are stored by reference,
    // depending on the CentralCache for backing store. This avoids a potential
    // contention-storm when destructing a scope, as the counter/gauge ref-count
    // decrement in allocator_impl.cc needs to hold the single allocator mutex.
    StatRefMap<Counter> counters_;
    StatRefMap<Gauge> gauges_;
    StatRefMap<TextReadout> text_readouts_;

    // Histograms also require holding a mutex while decrementing reference
    // counts. The only difference from other stats is that the histogram_set_
    // lives in the ThreadLocalStore object, rather than in
    // AllocatorImpl. Histograms are removed from that set when all scopes
    // referencing the histogram are dropped. Each ParentHistogram has a unique
    // index, which is not re-used during the process lifetime.
    //
    // There is also a tls_histogram_cache_ in the TlsCache object, which is
    // not tied to a scope. It maps from parent histogram's unique index to
    // a TlsHistogram. This enables continuity between same-named histograms
    // in same-named scopes. That scenario is common when re-creating scopes in
    // response to xDS.
    StatNameHashMap<ParentHistogramSharedPtr> parent_histograms_;

    // We keep a TLS cache of rejected stat names. This costs memory, but
    // reduces runtime overhead running the matcher. Moreover, once symbol
    // tables are integrated, rejection will need the fully elaborated string,
    // and it we need to take a global symbol-table lock to run. We keep this
    // StatName set here in the TLS cache to avoid taking a lock to compute
    // rejection.
    StatNameHashSet rejected_stats_;
  };

  struct CentralCacheEntry : public RefcountHelper {
    explicit CentralCacheEntry(SymbolTable& symbol_table) : symbol_table_(symbol_table) {}
    ~CentralCacheEntry();

    StatNameHashMap<CounterSharedPtr> counters_;
    StatNameHashMap<GaugeSharedPtr> gauges_;
    StatNameHashMap<ParentHistogramImplSharedPtr> histograms_;
    StatNameHashMap<TextReadoutSharedPtr> text_readouts_;
    StatNameStorageSet rejected_stats_;
    SymbolTable& symbol_table_;
  };
  using CentralCacheEntrySharedPtr = RefcountPtr<CentralCacheEntry>;

  struct ScopeImpl : public Scope {
    ScopeImpl(ThreadLocalStoreImpl& parent, StatName prefix);
    ~ScopeImpl() override;

    // Stats::Scope
    Counter& counterFromStatNameWithTags(const StatName& name,
                                         StatNameTagVectorOptConstRef tags) override;
    Gauge& gaugeFromStatNameWithTags(const StatName& name, StatNameTagVectorOptConstRef tags,
                                     Gauge::ImportMode import_mode) override;
    Histogram& histogramFromStatNameWithTags(const StatName& name,
                                             StatNameTagVectorOptConstRef tags,
                                             Histogram::Unit unit) override;
    TextReadout& textReadoutFromStatNameWithTags(const StatName& name,
                                                 StatNameTagVectorOptConstRef tags) override;
    ScopeSharedPtr createScope(const std::string& name) override;
    ScopeSharedPtr scopeFromStatName(StatName name) override;
    const SymbolTable& constSymbolTable() const final { return parent_.constSymbolTable(); }
    SymbolTable& symbolTable() final { return parent_.symbolTable(); }

    Counter& counterFromString(const std::string& name) override {
      StatNameManagedStorage storage(name, symbolTable());
      return counterFromStatName(storage.statName());
    }

    Gauge& gaugeFromString(const std::string& name, Gauge::ImportMode import_mode) override {
      StatNameManagedStorage storage(name, symbolTable());
      return gaugeFromStatName(storage.statName(), import_mode);
    }
    Histogram& histogramFromString(const std::string& name, Histogram::Unit unit) override {
      StatNameManagedStorage storage(name, symbolTable());
      return histogramFromStatName(storage.statName(), unit);
    }
    TextReadout& textReadoutFromString(const std::string& name) override {
      StatNameManagedStorage storage(name, symbolTable());
      return textReadoutFromStatName(storage.statName());
    }

    template <class StatMap, class StatFn> bool iterHelper(StatFn fn, const StatMap& map) const {
      for (auto& iter : map) {
        if (!fn(iter.second)) {
          return false;
        }
      }
      return true;
    }

    bool iterate(const IterateFn<Counter>& fn) const override {
      Thread::LockGuard lock(parent_.lock_);
      return iterateLockHeld(fn);
    }
    bool iterate(const IterateFn<Gauge>& fn) const override {
      Thread::LockGuard lock(parent_.lock_);
      return iterateLockHeld(fn);
    }
    bool iterate(const IterateFn<Histogram>& fn) const override {
      Thread::LockGuard lock(parent_.lock_);
      return iterateLockHeld(fn);
    }
    bool iterate(const IterateFn<TextReadout>& fn) const override {
      Thread::LockGuard lock(parent_.lock_);
      return iterateLockHeld(fn);
    }

    bool iterateLockHeld(const IterateFn<Counter>& fn) const {
      return iterHelper(fn, centralCacheLockHeld()->counters_);
    }
    bool iterateLockHeld(const IterateFn<Gauge>& fn) const {
      return iterHelper(fn, centralCacheLockHeld()->gauges_);
    }
    bool iterateLockHeld(const IterateFn<Histogram>& fn) const {
      return iterHelper(fn, centralCacheLockHeld()->histograms_);
    }
    bool iterateLockHeld(const IterateFn<TextReadout>& fn) const {
      return iterHelper(fn, centralCacheLockHeld()->text_readouts_);
    }
    ThreadLocalStoreImpl& store() override { return parent_; }
    const ThreadLocalStoreImpl& constStore() const override { return parent_; }

    // NOTE: The find methods assume that `name` is fully-qualified.
    // Implementations will not add the scope prefix.
    CounterOptConstRef findCounter(StatName name) const override;
    GaugeOptConstRef findGauge(StatName name) const override;
    HistogramOptConstRef findHistogram(StatName name) const override;
    TextReadoutOptConstRef findTextReadout(StatName name) const override;

    HistogramOptConstRef findHistogramLockHeld(StatName name) const;

    template <class StatType>
    using MakeStatFn = std::function<RefcountPtr<StatType>(
        Allocator&, StatName name, StatName tag_extracted_name, const StatNameTagVector& tags)>;

    /**
     * Makes a stat either by looking it up in the central cache,
     * generating it from the parent allocator, or as a last
     * result, creating it with the heap allocator.
     *
     * @param full_stat_name the full name of the stat with appended tags.
     * @param name_no_tags the full name of the stat (not tag extracted) without appended tags.
     * @param stat_name_tags the tags provided at creation time. If empty, tag extraction occurs.
     * @param central_cache_map a map from name to the desired object in the central cache.
     * @param make_stat a function to generate the stat object, called if it's not in cache.
     * @param tls_ref possibly null reference to a cache entry for this stat, which will be
     *     used if non-empty, or filled in if empty (and non-null).
     */
    template <class StatType>
    StatType& safeMakeStat(StatName full_stat_name, StatName name_no_tags,
                           const absl::optional<StatNameTagVector>& stat_name_tags,
                           StatNameHashMap<RefcountPtr<StatType>>& central_cache_map,
                           StatsMatcher::FastResult fast_reject_result,
                           StatNameStorageSet& central_rejected_stats,
                           MakeStatFn<StatType> make_stat, StatRefMap<StatType>* tls_cache,
                           StatNameHashSet* tls_rejected_stats, StatType& null_stat);

    template <class StatType>
    using StatTypeOptConstRef = absl::optional<std::reference_wrapper<const StatType>>;

    /**
     * Looks up an existing stat, populating the local cache if necessary. Does
     * not check the TLS or rejects, and does not create a stat if it does not
     * exist.
     *
     * @param name the full name of the stat (not tag extracted).
     * @param central_cache_map a map from name to the desired object in the central cache.
     * @return a reference to the stat, if it exists.
     */
    template <class StatType>
    StatTypeOptConstRef<StatType>
    findStatLockHeld(StatName name,
                     StatNameHashMap<RefcountPtr<StatType>>& central_cache_map) const {
      auto iter = central_cache_map.find(name);
      if (iter == central_cache_map.end()) {
        return absl::nullopt;
      }

      return std::cref(*iter->second);
    }

    StatName prefix() const override { return prefix_.statName(); }

    // Returns the central cache, asserting that the parent lock is held.
    //
    // When a ThreadLocalStore method takes lock_ and then accesses
    // scope->central_cache_, the analysis system cannot understand that the
    // scope's parent_.lock_ is held, so we assert that here.
    const CentralCacheEntrySharedPtr& centralCacheLockHeld() const
        ABSL_ASSERT_EXCLUSIVE_LOCK(parent_.lock_) {
      return central_cache_;
    }

    // Returns the central cache, bypassing thread analysis.
    //
    // This is used only when passing references to maps held in the central
    // cache to safeMakeStat, which takes the lock only if those maps are
    // actually referenced, due to the lookup missing the TLS cache.
    const CentralCacheEntrySharedPtr&
    centralCacheNoThreadAnalysis() const ABSL_NO_THREAD_SAFETY_ANALYSIS {
      return central_cache_;
    }

    const uint64_t scope_id_;
    ThreadLocalStoreImpl& parent_;

  private:
    StatNameStorage prefix_;
    mutable CentralCacheEntrySharedPtr central_cache_ ABSL_GUARDED_BY(parent_.lock_);
  };

  struct TlsCache : public ThreadLocal::ThreadLocalObject {
    TlsCacheEntry& insertScope(uint64_t scope_id);
    void eraseScopes(const std::vector<uint64_t>& scope_ids);
    void eraseHistograms(const std::vector<uint64_t>& histograms);

    // The TLS scope cache is keyed by scope ID. This is used to avoid complex circular references
    // during scope destruction. An ID is required vs. using the address of the scope pointer
    // because it's possible that the memory allocator will recycle the scope pointer immediately
    // upon destruction, leading to a situation in which a new scope with the same address is used
    // to reference the cache, and then subsequently cache flushed, leaving nothing in the central
    // store. See the overview for more information. This complexity is required for lockless
    // operation in the fast path.
    absl::flat_hash_map<uint64_t, TlsCacheEntry> scope_cache_;

    // Maps from histogram ID (monotonically increasing) to a TLS histogram.
    absl::flat_hash_map<uint64_t, TlsHistogramSharedPtr> tls_histogram_cache_;
  };

  using ScopeImplSharedPtr = std::shared_ptr<ScopeImpl>;

  /**
   * Calls fn_lock_held for every scope with, lock_ held. This avoids iterate/destruct
   * races for scopes.
   *
   * @param fn_lock_held function to be called, with lock_ held, on every scope, until
   *   fn_lock_held() returns false.
   * @return true if the iteration completed with fn_lock_held never returning false.
   */
  bool iterateScopes(const std::function<bool(const ScopeImplSharedPtr&)> fn_lock_held) const {
    Thread::LockGuard lock(lock_);
    return iterateScopesLockHeld(fn_lock_held);
  }

  bool iterateScopesLockHeld(const std::function<bool(const ScopeImplSharedPtr&)> fn) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  // The Store versions of iterate cover all the scopes in the store.
  template <class StatFn> bool iterHelper(StatFn fn) const {
    return iterateScopes(
        [fn](const ScopeImplSharedPtr& scope) -> bool { return scope->iterateLockHeld(fn); });
  }

  std::string getTagsForName(const std::string& name, TagVector& tags) const;
  void clearScopesFromCaches();
  void clearHistogramsFromCaches();
  void releaseScopeCrossThread(ScopeImpl* scope);
  void mergeInternal(PostMergeCb merge_cb);
  bool slowRejects(StatsMatcher::FastResult fast_reject_result, StatName name) const;
  bool rejects(StatName name) const { return stats_matcher_->rejects(name); }
  StatsMatcher::FastResult fastRejects(StatName name) const;
  bool rejectsAll() const { return stats_matcher_->rejectsAll(); }
  template <class StatMapClass, class StatListClass>
  void removeRejectedStats(StatMapClass& map, StatListClass& list);
  template <class StatSharedPtr>
  void removeRejectedStats(StatNameHashMap<StatSharedPtr>& map,
                           std::function<void(const StatSharedPtr&)> f_deletion);
  bool checkAndRememberRejection(StatName name, StatsMatcher::FastResult fast_reject_result,
                                 StatNameStorageSet& central_rejected_stats,
                                 StatNameHashSet* tls_rejected_stats);
  TlsCache& tlsCache() { return **tls_cache_; }
  void addScope(std::shared_ptr<ScopeImpl>& new_scope);

  OptRef<SinkPredicates> sink_predicates_;
  Allocator& alloc_;
  Event::Dispatcher* main_thread_dispatcher_{};
  using TlsCacheSlot = ThreadLocal::TypedSlotPtr<TlsCache>;
  ThreadLocal::TypedSlotPtr<TlsCache> tls_cache_;
  mutable Thread::MutexBasicLockable lock_;
  absl::flat_hash_map<ScopeImpl*, std::weak_ptr<ScopeImpl>> scopes_ ABSL_GUARDED_BY(lock_);
  ScopeSharedPtr default_scope_;
  std::list<std::reference_wrapper<Sink>> timer_sinks_;
  TagProducerPtr tag_producer_;
  StatsMatcherPtr stats_matcher_;
  HistogramSettingsConstPtr histogram_settings_;
  std::atomic<bool> threading_ever_initialized_{};
  std::atomic<bool> shutting_down_{};
  std::atomic<bool> merge_in_progress_{};
  OptRef<ThreadLocal::Instance> tls_;

  NullCounterImpl null_counter_;
  NullGaugeImpl null_gauge_;
  NullHistogramImpl null_histogram_;
  NullTextReadoutImpl null_text_readout_;

  mutable Thread::ThreadSynchronizer sync_;
  std::atomic<uint64_t> next_scope_id_{};
  uint64_t next_histogram_id_ ABSL_GUARDED_BY(hist_mutex_) = 0;

  StatNameSetPtr well_known_tags_;

  mutable Thread::MutexBasicLockable hist_mutex_;
  StatSet<ParentHistogramImpl> histogram_set_ ABSL_GUARDED_BY(hist_mutex_);
  StatSet<ParentHistogramImpl> sinked_histograms_ ABSL_GUARDED_BY(hist_mutex_);

  // Retain storage for deleted stats; these are no longer in maps because the
  // matcher-pattern was established after they were created. Since the stats
  // are held by reference in code that expects them to be there, we can't
  // actually delete the stats.
  //
  // It seems like it would be better to have each client that expects a stat
  // to exist to hold it as (e.g.) a CounterSharedPtr rather than a Counter&
  // but that would be fairly complex to change.
  std::vector<HistogramSharedPtr> deleted_histograms_ ABSL_GUARDED_BY(lock_);

  // Scope IDs and central cache entries that are queued for cross-scope release.
  // Because there can be a large number of scopes, all of which are released at once,
  // (e.g. when a scope is deleted), it is more efficient to batch their cleanup,
  // which would otherwise entail a post() per scope per thread.
  std::vector<uint64_t> scopes_to_cleanup_ ABSL_GUARDED_BY(lock_);
  std::vector<CentralCacheEntrySharedPtr> central_cache_entries_to_cleanup_ ABSL_GUARDED_BY(lock_);

  // Histograms IDs that are queued for cross-scope release. Because there
  // can be a large number of histograms, all of which are released at once,
  // (e.g. when a scope is deleted), it is likely more efficient to batch their
  // cleanup, which would otherwise entail a post() per histogram per thread.
  std::vector<uint64_t> histograms_to_cleanup_ ABSL_GUARDED_BY(hist_mutex_);
};

using ThreadLocalStoreImplPtr = std::unique_ptr<ThreadLocalStoreImpl>;

} // namespace Stats
} // namespace Envoy
