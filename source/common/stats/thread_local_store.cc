#include "source/common/stats/thread_local_store.h"

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/stats/allocator.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/sink.h"
#include "envoy/stats/stats.h"

#include "source/common/common/lock_guard.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/stats/histogram_impl.h"
#include "source/common/stats/stats_matcher_impl.h"
#include "source/common/stats/tag_producer_impl.h"
#include "source/common/stats/tag_utility.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Stats {

const char ThreadLocalStoreImpl::DeleteScopeSync[] = "delete-scope";
const char ThreadLocalStoreImpl::IterateScopeSync[] = "iterate-scope";
const char ThreadLocalStoreImpl::MainDispatcherCleanupSync[] = "main-dispatcher-cleanup";

ThreadLocalStoreImpl::ThreadLocalStoreImpl(Allocator& alloc)
    : alloc_(alloc), tag_producer_(std::make_unique<TagProducerImpl>()),
      stats_matcher_(std::make_unique<StatsMatcherImpl>()),
      histogram_settings_(std::make_unique<HistogramSettingsImpl>()),
      null_counter_(alloc.symbolTable()), null_gauge_(alloc.symbolTable()),
      null_histogram_(alloc.symbolTable()), null_text_readout_(alloc.symbolTable()),
      well_known_tags_(alloc.symbolTable().makeSet("well_known_tags")) {
  for (const auto& desc : Config::TagNames::get().descriptorVec()) {
    well_known_tags_->rememberBuiltin(desc.name_);
  }
  StatNameManagedStorage empty("", alloc.symbolTable());
  auto new_scope = std::make_shared<ScopeImpl>(*this, StatName(empty.statName()));
  addScope(new_scope);
  default_scope_ = new_scope;
}

ThreadLocalStoreImpl::~ThreadLocalStoreImpl() {
  ASSERT(shutting_down_ || !threading_ever_initialized_);
  default_scope_.reset();
  ASSERT(scopes_.empty());
  ASSERT(scopes_to_cleanup_.empty());
  ASSERT(central_cache_entries_to_cleanup_.empty());
  ASSERT(histograms_to_cleanup_.empty());
}

void ThreadLocalStoreImpl::setHistogramSettings(HistogramSettingsConstPtr&& histogram_settings) {
  iterateScopes([](const ScopeImplSharedPtr& scope) -> bool {
    ASSERT(scope->centralCacheLockHeld()->histograms_.empty());
    return true;
  });
  histogram_settings_ = std::move(histogram_settings);
}

void ThreadLocalStoreImpl::setStatsMatcher(StatsMatcherPtr&& stats_matcher) {
  stats_matcher_ = std::move(stats_matcher);
  if (stats_matcher_->acceptsAll()) {
    return;
  }

  // The Filesystem and potentially other stat-registering objects are
  // constructed prior to the stat-matcher, and those add stats
  // in the default_scope. There should be no requests, so there will
  // be no copies in TLS caches.
  Thread::LockGuard lock(lock_);
  const uint32_t first_histogram_index = deleted_histograms_.size();
  iterateScopesLockHeld([this](const ScopeImplSharedPtr& scope) ABSL_EXCLUSIVE_LOCKS_REQUIRED(
                            lock_) -> bool {
    const CentralCacheEntrySharedPtr& central_cache = scope->centralCacheLockHeld();
    removeRejectedStats<CounterSharedPtr>(central_cache->counters_,
                                          [this](const CounterSharedPtr& counter) mutable {
                                            alloc_.markCounterForDeletion(counter);
                                          });
    removeRejectedStats<GaugeSharedPtr>(
        central_cache->gauges_,
        [this](const GaugeSharedPtr& gauge) mutable { alloc_.markGaugeForDeletion(gauge); });
    removeRejectedStats(central_cache->histograms_, deleted_histograms_);
    removeRejectedStats<TextReadoutSharedPtr>(
        central_cache->text_readouts_, [this](const TextReadoutSharedPtr& text_readout) mutable {
          alloc_.markTextReadoutForDeletion(text_readout);
        });
    return true;
  });

  // Remove any newly rejected histograms from histogram_set_.
  {
    Thread::LockGuard hist_lock(hist_mutex_);
    for (uint32_t i = first_histogram_index; i < deleted_histograms_.size(); ++i) {
      uint32_t erased = histogram_set_.erase(deleted_histograms_[i].get());
      ASSERT(erased == 1);
      sinked_histograms_.erase(deleted_histograms_[i].get());
    }
  }
}

template <class StatMapClass, class StatListClass>
void ThreadLocalStoreImpl::removeRejectedStats(StatMapClass& map, StatListClass& list) {
  StatNameVec remove_list;
  for (auto& stat : map) {
    if (rejects(stat.first)) {
      remove_list.push_back(stat.first);
    }
  }
  for (StatName stat_name : remove_list) {
    auto iter = map.find(stat_name);
    ASSERT(iter != map.end());
    list.push_back(iter->second); // Save SharedPtr to the list to avoid invalidating refs to stat.
    map.erase(iter);
  }
}

template <class StatSharedPtr>
void ThreadLocalStoreImpl::removeRejectedStats(
    StatNameHashMap<StatSharedPtr>& map, std::function<void(const StatSharedPtr&)> f_deletion) {
  StatNameVec remove_list;
  for (auto& stat : map) {
    if (rejects(stat.first)) {
      remove_list.push_back(stat.first);
    }
  }
  for (StatName stat_name : remove_list) {
    auto iter = map.find(stat_name);
    ASSERT(iter != map.end());
    f_deletion(iter->second);
    map.erase(iter);
  }
}

StatsMatcher::FastResult ThreadLocalStoreImpl::fastRejects(StatName stat_name) const {
  return stats_matcher_->fastRejects(stat_name);
}

bool ThreadLocalStoreImpl::slowRejects(StatsMatcher::FastResult fast_reject_result,
                                       StatName stat_name) const {
  return stats_matcher_->slowRejects(fast_reject_result, stat_name);
}

std::vector<CounterSharedPtr> ThreadLocalStoreImpl::counters() const {
  // Handle de-dup due to overlapping scopes.
  std::vector<CounterSharedPtr> ret;
  forEachCounter([&ret](std::size_t size) { ret.reserve(size); },
                 [&ret](Counter& counter) { ret.emplace_back(CounterSharedPtr(&counter)); });
  return ret;
}

ScopeSharedPtr ThreadLocalStoreImpl::ScopeImpl::createScope(const std::string& name) {
  StatNameManagedStorage stat_name_storage(Utility::sanitizeStatsName(name), symbolTable());
  return scopeFromStatName(stat_name_storage.statName());
}

ScopeSharedPtr ThreadLocalStoreImpl::ScopeImpl::scopeFromStatName(StatName name) {
  SymbolTable::StoragePtr joined = symbolTable().join({prefix_.statName(), name});
  auto new_scope = std::make_shared<ScopeImpl>(parent_, StatName(joined.get()));
  parent_.addScope(new_scope);
  return new_scope;
}

void ThreadLocalStoreImpl::addScope(std::shared_ptr<ScopeImpl>& new_scope) {
  Thread::LockGuard lock(lock_);
  scopes_[new_scope.get()] = std::weak_ptr<ScopeImpl>(new_scope);
}

std::vector<GaugeSharedPtr> ThreadLocalStoreImpl::gauges() const {
  // Handle de-dup due to overlapping scopes.
  std::vector<GaugeSharedPtr> ret;
  forEachGauge([&ret](std::size_t size) { ret.reserve(size); },
               [&ret](Gauge& gauge) { ret.emplace_back(GaugeSharedPtr(&gauge)); });
  return ret;
}

std::vector<TextReadoutSharedPtr> ThreadLocalStoreImpl::textReadouts() const {
  // Handle de-dup due to overlapping scopes.
  std::vector<TextReadoutSharedPtr> ret;
  forEachTextReadout(
      [&ret](std::size_t size) { ret.reserve(size); },
      [&ret](TextReadout& text_readout) { ret.emplace_back(TextReadoutSharedPtr(&text_readout)); });
  return ret;
}

std::vector<ParentHistogramSharedPtr> ThreadLocalStoreImpl::histograms() const {
  std::vector<ParentHistogramSharedPtr> ret;
  forEachHistogram([&ret](std::size_t size) mutable { ret.reserve(size); },
                   [&ret](ParentHistogram& histogram) mutable {
                     ret.emplace_back(ParentHistogramSharedPtr(&histogram));
                   });
  return ret;
}

void ThreadLocalStoreImpl::initializeThreading(Event::Dispatcher& main_thread_dispatcher,
                                               ThreadLocal::Instance& tls) {
  threading_ever_initialized_ = true;
  main_thread_dispatcher_ = &main_thread_dispatcher;
  tls_cache_ = ThreadLocal::TypedSlot<TlsCache>::makeUnique(tls);
  tls_cache_->set(
      [](Event::Dispatcher&) -> std::shared_ptr<TlsCache> { return std::make_shared<TlsCache>(); });
  tls_ = tls;
}

void ThreadLocalStoreImpl::shutdownThreading() {
  // This will block both future cache fills as well as cache flushes.
  shutting_down_ = true;
  ASSERT(!tls_.has_value() || tls_->isShutdown());

  // We can't call runOnAllThreads here as global threading has already been shutdown. It is okay
  // to simply clear the scopes and central cache entries here as they will be cleaned up during
  // thread local data cleanup in InstanceImpl::shutdownThread().
  {
    Thread::LockGuard lock(lock_);
    scopes_to_cleanup_.clear();
    central_cache_entries_to_cleanup_.clear();
  }

  Thread::LockGuard lock(hist_mutex_);
  for (ParentHistogramImpl* histogram : histogram_set_) {
    histogram->setShuttingDown(true);
  }
  histogram_set_.clear();
  sinked_histograms_.clear();
}

void ThreadLocalStoreImpl::mergeHistograms(PostMergeCb merge_complete_cb) {
  if (!shutting_down_) {
    ASSERT(!merge_in_progress_);
    merge_in_progress_ = true;
    tls_cache_->runOnAllThreads(
        [](OptRef<TlsCache> tls_cache) {
          for (const auto& id_hist : tls_cache->tls_histogram_cache_) {
            const TlsHistogramSharedPtr& tls_hist = id_hist.second;
            tls_hist->beginMerge();
          }
        },
        [this, merge_complete_cb]() -> void { mergeInternal(merge_complete_cb); });
  } else {
    // If server is shutting down, just call the callback to allow flush to continue.
    merge_complete_cb();
  }
}

void ThreadLocalStoreImpl::mergeInternal(PostMergeCb merge_complete_cb) {
  if (!shutting_down_) {
    forEachHistogram(nullptr, [](ParentHistogram& histogram) { histogram.merge(); });
    merge_complete_cb();
    merge_in_progress_ = false;
  }
}

ThreadLocalStoreImpl::CentralCacheEntry::~CentralCacheEntry() {
  // Assert that the symbol-table is valid, so we get good test coverage of
  // the validity of the symbol table at the time this destructor runs. This
  // is because many tests will not populate rejected_stats_.
  ASSERT(symbol_table_.toString(StatNameManagedStorage("Hello.world", symbol_table_).statName()) ==
         "Hello.world");
  rejected_stats_.free(symbol_table_); // NOLINT(clang-analyzer-unix.Malloc)
}

void ThreadLocalStoreImpl::releaseScopeCrossThread(ScopeImpl* scope) {
  Thread::ReleasableLockGuard lock(lock_);
  ASSERT(scopes_.count(scope) == 1);
  scopes_.erase(scope);

  // This method is called directly from the ScopeImpl destructor, but we can't
  // destroy scope->central_cache_ until all the TLS caches are be destroyed, as
  // the TLS caches reference the Counters and Gauges owned by the central
  // cache. We don't want the maps in the TLS caches to bump the
  // reference-count, as decrementing the count requires an allocator lock,
  // which would cause a storm of contention during scope destruction.
  //
  // So instead we have a 2-phase destroy:
  //   1. destroy all the TLS caches
  //   2. destroy the central cache.
  //
  // Since this is called from ScopeImpl's destructor, we must bump the
  // ref-count of the central-cache by copying to a local scoped pointer, and
  // keep that reference alive until all the TLS caches are clear. This is done by keeping a
  // separate vector of shared_ptrs which will be destructed once all threads have completed.

  // This can happen from any thread. We post() back to the main thread which will initiate the
  // cache flush operation.
  if (!shutting_down_ && main_thread_dispatcher_) {
    // Clear scopes in a batch. It's possible that many different scopes will be deleted at
    // the same time, before the main thread gets a chance to run cleanScopesFromCaches. If a new
    // scope is deleted before that post runs, we add it to our list of scopes to clear, and there
    // is no need to issue another post. This greatly reduces the overhead when there are tens of
    // thousands of scopes to clear in a short period. i.e.: VHDS updates with tens of thousands of
    // VirtualHosts.
    bool need_post = scopes_to_cleanup_.empty();
    scopes_to_cleanup_.push_back(scope->scope_id_);
    central_cache_entries_to_cleanup_.push_back(scope->centralCacheLockHeld());
    lock.release();

    if (need_post) {
      main_thread_dispatcher_->post([this]() {
        sync_.syncPoint(MainDispatcherCleanupSync);
        clearScopesFromCaches();
      });
    }
  }
}

void ThreadLocalStoreImpl::releaseHistogramCrossThread(uint64_t histogram_id) {
  // This can happen from any thread. We post() back to the main thread which will initiate the
  // cache flush operation.
  if (!shutting_down_ && main_thread_dispatcher_) {
    // It's possible that many different histograms will be deleted at the same
    // time, before the main thread gets a chance to run
    // clearHistogramsFromCaches. If a new histogram is deleted before that
    // post runs, we add it to our list of histograms to clear, and there's no
    // need to issue another post.
    bool need_post = false;
    {
      Thread::LockGuard lock(hist_mutex_);
      need_post = histograms_to_cleanup_.empty();
      histograms_to_cleanup_.push_back(histogram_id);
    }
    if (need_post) {
      main_thread_dispatcher_->post([this]() { clearHistogramsFromCaches(); });
    }
  }
}

ThreadLocalStoreImpl::TlsCacheEntry&
ThreadLocalStoreImpl::TlsCache::insertScope(uint64_t scope_id) {
  return scope_cache_[scope_id];
}

void ThreadLocalStoreImpl::TlsCache::eraseScopes(const std::vector<uint64_t>& scope_ids) {
  for (uint64_t scope_id : scope_ids) {
    scope_cache_.erase(scope_id);
  }
}

void ThreadLocalStoreImpl::TlsCache::eraseHistograms(const std::vector<uint64_t>& histograms) {
  // This is called for every histogram in every thread, even though the
  // histogram may not have been cached in each thread yet. So we don't
  // want to check whether the erase() call erased anything.
  for (uint64_t histogram_id : histograms) {
    tls_histogram_cache_.erase(histogram_id);
  }
}

void ThreadLocalStoreImpl::clearScopesFromCaches() {
  // If we are shutting down we no longer perform cache flushes as workers may be shutting down
  // at the same time.
  if (!shutting_down_) {
    // Perform a cache flush on all threads.

    // Capture all the pending scope ids in a local, clearing the list held in
    // this. Once this occurs, if a new scope is deleted, a new post will be
    // required.
    auto scope_ids = std::make_shared<std::vector<uint64_t>>();
    // Capture all the central cache entries for scopes we're deleting. These will be freed after
    // all threads have completed.
    auto central_caches = std::make_shared<std::vector<CentralCacheEntrySharedPtr>>();
    {
      Thread::LockGuard lock(lock_);
      *scope_ids = std::move(scopes_to_cleanup_);
      scopes_to_cleanup_.clear();
      *central_caches = std::move(central_cache_entries_to_cleanup_);
      central_cache_entries_to_cleanup_.clear();
    }

    tls_cache_->runOnAllThreads(
        [scope_ids](OptRef<TlsCache> tls_cache) { tls_cache->eraseScopes(*scope_ids); },
        [central_caches]() { /* Holds onto central_caches until all tls caches are clear */ });
  }
}

void ThreadLocalStoreImpl::clearHistogramsFromCaches() {
  // If we are shutting down we no longer perform cache flushes as workers may be shutting down
  // at the same time.
  if (!shutting_down_) {
    // Move the histograms pending cleanup into a local variable. Future histogram deletions will be
    // batched until the next time this function is called.
    auto histograms = std::make_shared<std::vector<uint64_t>>();
    {
      Thread::LockGuard lock(hist_mutex_);
      histograms->swap(histograms_to_cleanup_);
    }

    tls_cache_->runOnAllThreads(
        [histograms](OptRef<TlsCache> tls_cache) { tls_cache->eraseHistograms(*histograms); });
  }
}

ThreadLocalStoreImpl::ScopeImpl::ScopeImpl(ThreadLocalStoreImpl& parent, StatName prefix)
    : scope_id_(parent.next_scope_id_++), parent_(parent),
      prefix_(prefix, parent.alloc_.symbolTable()),
      central_cache_(new CentralCacheEntry(parent.alloc_.symbolTable())) {}

ThreadLocalStoreImpl::ScopeImpl::~ScopeImpl() {
  // Helps reproduce a previous race condition by pausing here in tests while we
  // loop over scopes. 'this' will not have been removed from the scopes_ table
  // yet, so we need to be careful.
  parent_.sync_.syncPoint(DeleteScopeSync);

  // Note that scope iteration is thread-safe due to the lock held in
  // releaseScopeCrossThread. For more details see the comment in
  // `ThreadLocalStoreImpl::iterHelper`, and the lock it takes prior to the loop.
  parent_.releaseScopeCrossThread(this);
  prefix_.free(symbolTable());
}

// Helper for managing the potential truncation of tags from the metric names and
// converting them to StatName. Making the tag extraction optional within this class simplifies the
// RAII ergonomics (as opposed to making the construction of this object conditional).
//
// The StatNameTagVector returned by this object will be valid as long as this object is in scope
// and the provided stat_name_tags are valid.
//
// When tag extraction is not done, this class is just a passthrough for the provided name/tags.
class StatNameTagHelper {
public:
  StatNameTagHelper(ThreadLocalStoreImpl& tls, StatName name,
                    const absl::optional<StatNameTagVector>& stat_name_tags)
      : pool_(tls.symbolTable()), stat_name_tags_(stat_name_tags.value_or(StatNameTagVector())) {
    if (!stat_name_tags) {
      TagVector tags;
      tag_extracted_name_ =
          pool_.add(tls.tagProducer().produceTags(tls.symbolTable().toString(name), tags));
      StatName empty;
      for (const auto& tag : tags) {
        StatName tag_name = tls.wellKnownTags().getBuiltin(tag.name_, empty);
        if (tag_name.empty()) {
          tag_name = pool_.add(tag.name_);
        }
        stat_name_tags_.emplace_back(tag_name, pool_.add(tag.value_));
      }
    } else {
      tag_extracted_name_ = name;
    }
  }

  const StatNameTagVector& statNameTags() const { return stat_name_tags_; }
  StatName tagExtractedName() const { return tag_extracted_name_; }

private:
  StatNamePool pool_;
  StatNameTagVector stat_name_tags_;
  StatName tag_extracted_name_;
};

bool ThreadLocalStoreImpl::checkAndRememberRejection(StatName name,
                                                     StatsMatcher::FastResult fast_reject_result,
                                                     StatNameStorageSet& central_rejected_stats,
                                                     StatNameHashSet* tls_rejected_stats) {
  if (stats_matcher_->acceptsAll()) {
    return false;
  }

  auto iter = central_rejected_stats.find(name);
  const StatNameStorage* rejected_name = nullptr;
  if (iter != central_rejected_stats.end()) {
    rejected_name = &(*iter);
  } else {
    if (slowRejects(fast_reject_result, name)) {
      auto insertion = central_rejected_stats.insert(StatNameStorage(name, symbolTable()));
      const StatNameStorage& rejected_name_ref = *(insertion.first);
      rejected_name = &rejected_name_ref;
    }
  }
  if (rejected_name != nullptr) {
    if (tls_rejected_stats != nullptr) {
      tls_rejected_stats->insert(rejected_name->statName());
    }
    return true;
  }
  return false;
}

template <class StatType>
StatType& ThreadLocalStoreImpl::ScopeImpl::safeMakeStat(
    StatName full_stat_name, StatName name_no_tags,
    const absl::optional<StatNameTagVector>& stat_name_tags,
    StatNameHashMap<RefcountPtr<StatType>>& central_cache_map,
    StatsMatcher::FastResult fast_reject_result, StatNameStorageSet& central_rejected_stats,
    MakeStatFn<StatType> make_stat, StatRefMap<StatType>* tls_cache,
    StatNameHashSet* tls_rejected_stats, StatType& null_stat) {

  if (tls_rejected_stats != nullptr &&
      tls_rejected_stats->find(full_stat_name) != tls_rejected_stats->end()) {
    return null_stat;
  }

  // If we have a valid cache entry, return it.
  if (tls_cache) {
    auto pos = tls_cache->find(full_stat_name);
    if (pos != tls_cache->end()) {
      return pos->second;
    }
  }

  // We must now look in the central store so we must be locked. We grab a reference to the
  // central store location. It might contain nothing. In this case, we allocate a new stat.
  Thread::LockGuard lock(parent_.lock_);
  auto iter = central_cache_map.find(full_stat_name);
  RefcountPtr<StatType>* central_ref = nullptr;
  if (iter != central_cache_map.end()) {
    central_ref = &(iter->second);
  } else if (parent_.checkAndRememberRejection(full_stat_name, fast_reject_result,
                                               central_rejected_stats, tls_rejected_stats)) {
    return null_stat;
  } else {
    StatNameTagHelper tag_helper(parent_, name_no_tags, stat_name_tags);

    RefcountPtr<StatType> stat = make_stat(
        parent_.alloc_, full_stat_name, tag_helper.tagExtractedName(), tag_helper.statNameTags());
    ASSERT(stat != nullptr);
    central_ref = &central_cache_map[stat->statName()];
    *central_ref = stat;
  }

  // If we have a TLS cache, insert the stat.
  StatType& ret = **central_ref;
  if (tls_cache) {
    tls_cache->insert(std::make_pair(ret.statName(), std::reference_wrapper<StatType>(ret)));
  }

  // Finally we return the reference.
  return ret;
}

Counter& ThreadLocalStoreImpl::ScopeImpl::counterFromStatNameWithTags(
    const StatName& name, StatNameTagVectorOptConstRef stat_name_tags) {
  if (parent_.rejectsAll()) {
    return parent_.null_counter_;
  }

  // Determine the final name based on the prefix and the passed name.
  TagUtility::TagStatNameJoiner joiner(prefix_.statName(), name, stat_name_tags, symbolTable());
  Stats::StatName final_stat_name = joiner.nameWithTags();

  StatsMatcher::FastResult fast_reject_result = parent_.fastRejects(final_stat_name);
  if (fast_reject_result == StatsMatcher::FastResult::Rejects) {
    return parent_.null_counter_;
  }

  // We now find the TLS cache. This might remain null if we don't have TLS
  // initialized currently.
  StatRefMap<Counter>* tls_cache = nullptr;
  StatNameHashSet* tls_rejected_stats = nullptr;
  if (!parent_.shutting_down_ && parent_.tls_cache_) {
    TlsCacheEntry& entry = parent_.tlsCache().insertScope(this->scope_id_);
    tls_cache = &entry.counters_;
    tls_rejected_stats = &entry.rejected_stats_;
  }

  const CentralCacheEntrySharedPtr& central_cache = centralCacheNoThreadAnalysis();
  return safeMakeStat<Counter>(
      final_stat_name, joiner.tagExtractedName(), stat_name_tags, central_cache->counters_,
      fast_reject_result, central_cache->rejected_stats_,
      [](Allocator& allocator, StatName name, StatName tag_extracted_name,
         const StatNameTagVector& tags) -> CounterSharedPtr {
        return allocator.makeCounter(name, tag_extracted_name, tags);
      },
      tls_cache, tls_rejected_stats, parent_.null_counter_);
}

void ThreadLocalStoreImpl::deliverHistogramToSinks(const Histogram& histogram, uint64_t value) {
  // Thread local deliveries must be blocked outright for histograms and timers during shutdown.
  // This is because the sinks may end up trying to create new connections via the thread local
  // cluster manager which may already be destroyed (there is no way to sequence this because the
  // cluster manager destroying can create deliveries). We special case this explicitly to avoid
  // having to implement a shutdown() method (or similar) on every TLS object.
  if (shutting_down_) {
    return;
  }

  for (Sink& sink : timer_sinks_) {
    sink.onHistogramComplete(histogram, value);
  }
}

Gauge& ThreadLocalStoreImpl::ScopeImpl::gaugeFromStatNameWithTags(
    const StatName& name, StatNameTagVectorOptConstRef stat_name_tags,
    Gauge::ImportMode import_mode) {
  // If a gauge is "hidden" it should not be rejected as these are used for deferred stats.
  if (parent_.rejectsAll() && import_mode != Gauge::ImportMode::HiddenAccumulate) {
    return parent_.null_gauge_;
  }

  // See comments in counter(). There is no super clean way (via templates or otherwise) to
  // share this code so I'm leaving it largely duplicated for now.
  TagUtility::TagStatNameJoiner joiner(prefix_.statName(), name, stat_name_tags, symbolTable());
  StatName final_stat_name = joiner.nameWithTags();

  StatsMatcher::FastResult fast_reject_result;
  if (import_mode != Gauge::ImportMode::HiddenAccumulate) {
    fast_reject_result = parent_.fastRejects(final_stat_name);
  } else {
    fast_reject_result = StatsMatcher::FastResult::Matches;
  }
  if (fast_reject_result == StatsMatcher::FastResult::Rejects) {
    return parent_.null_gauge_;
  }

  StatRefMap<Gauge>* tls_cache = nullptr;
  StatNameHashSet* tls_rejected_stats = nullptr;
  if (!parent_.shutting_down_ && parent_.tls_cache_) {
    TlsCacheEntry& entry = parent_.tlsCache().insertScope(this->scope_id_);
    tls_cache = &entry.gauges_;
    tls_rejected_stats = &entry.rejected_stats_;
  }

  const CentralCacheEntrySharedPtr& central_cache = centralCacheNoThreadAnalysis();
  Gauge& gauge = safeMakeStat<Gauge>(
      final_stat_name, joiner.tagExtractedName(), stat_name_tags, central_cache->gauges_,
      fast_reject_result, central_cache->rejected_stats_,
      [import_mode](Allocator& allocator, StatName name, StatName tag_extracted_name,
                    const StatNameTagVector& tags) -> GaugeSharedPtr {
        return allocator.makeGauge(name, tag_extracted_name, tags, import_mode);
      },
      tls_cache, tls_rejected_stats, parent_.null_gauge_);
  gauge.mergeImportMode(import_mode);
  return gauge;
}

Histogram& ThreadLocalStoreImpl::ScopeImpl::histogramFromStatNameWithTags(
    const StatName& name, StatNameTagVectorOptConstRef stat_name_tags, Histogram::Unit unit) {
  // See safety analysis comment in counterFromStatNameWithTags above.

  if (parent_.rejectsAll()) {
    return parent_.null_histogram_;
  }

  // See comments in counter(). There is no super clean way (via templates or otherwise) to
  // share this code so I'm leaving it largely duplicated for now.
  TagUtility::TagStatNameJoiner joiner(prefix_.statName(), name, stat_name_tags, symbolTable());
  StatName final_stat_name = joiner.nameWithTags();

  StatsMatcher::FastResult fast_reject_result = parent_.fastRejects(final_stat_name);
  if (fast_reject_result == StatsMatcher::FastResult::Rejects) {
    return parent_.null_histogram_;
  }

  StatNameHashMap<ParentHistogramSharedPtr>* tls_cache = nullptr;
  StatNameHashSet* tls_rejected_stats = nullptr;
  if (!parent_.shutting_down_ && parent_.tls_cache_) {
    TlsCacheEntry& entry = parent_.tlsCache().insertScope(this->scope_id_);
    tls_cache = &entry.parent_histograms_;
    auto iter = tls_cache->find(final_stat_name);
    if (iter != tls_cache->end()) {
      return *iter->second;
    }
    tls_rejected_stats = &entry.rejected_stats_;
    if (tls_rejected_stats->find(final_stat_name) != tls_rejected_stats->end()) {
      return parent_.null_histogram_;
    }
  }

  Thread::LockGuard lock(parent_.lock_);
  const CentralCacheEntrySharedPtr& central_cache = centralCacheNoThreadAnalysis();
  auto iter = central_cache->histograms_.find(final_stat_name);
  ParentHistogramImplSharedPtr* central_ref = nullptr;
  if (iter != central_cache->histograms_.end()) {
    central_ref = &iter->second;
  } else if (parent_.checkAndRememberRejection(final_stat_name, fast_reject_result,
                                               central_cache->rejected_stats_,
                                               tls_rejected_stats)) {
    return parent_.null_histogram_;
  } else {
    StatNameTagHelper tag_helper(parent_, joiner.tagExtractedName(), stat_name_tags);

    ConstSupportedBuckets* buckets = nullptr;
    buckets = &parent_.histogram_settings_->buckets(symbolTable().toString(final_stat_name));

    RefcountPtr<ParentHistogramImpl> stat;
    {
      Thread::LockGuard lock(parent_.hist_mutex_);
      auto iter = parent_.histogram_set_.find(final_stat_name);
      if (iter != parent_.histogram_set_.end()) {
        stat = RefcountPtr<ParentHistogramImpl>(*iter);
      } else {
        stat = new ParentHistogramImpl(final_stat_name, unit, parent_,
                                       tag_helper.tagExtractedName(), tag_helper.statNameTags(),
                                       *buckets, parent_.next_histogram_id_++);
        if (!parent_.shutting_down_) {
          parent_.histogram_set_.insert(stat.get());
          if (parent_.sink_predicates_.has_value() &&
              parent_.sink_predicates_->includeHistogram(*stat)) {
            parent_.sinked_histograms_.insert(stat.get());
          }
        }
      }
    }

    central_ref = &central_cache->histograms_[stat->statName()];
    *central_ref = stat;
  }

  if (tls_cache != nullptr) {
    tls_cache->insert(std::make_pair((*central_ref)->statName(), *central_ref));
  }
  return **central_ref;
}

TextReadout& ThreadLocalStoreImpl::ScopeImpl::textReadoutFromStatNameWithTags(
    const StatName& name, StatNameTagVectorOptConstRef stat_name_tags) {
  if (parent_.rejectsAll()) {
    return parent_.null_text_readout_;
  }

  // Determine the final name based on the prefix and the passed name.
  TagUtility::TagStatNameJoiner joiner(prefix_.statName(), name, stat_name_tags, symbolTable());
  Stats::StatName final_stat_name = joiner.nameWithTags();

  StatsMatcher::FastResult fast_reject_result = parent_.fastRejects(final_stat_name);
  if (fast_reject_result == StatsMatcher::FastResult::Rejects) {
    return parent_.null_text_readout_;
  }

  // We now find the TLS cache. This might remain null if we don't have TLS
  // initialized currently.
  StatRefMap<TextReadout>* tls_cache = nullptr;
  StatNameHashSet* tls_rejected_stats = nullptr;
  if (!parent_.shutting_down_ && parent_.tls_cache_) {
    TlsCacheEntry& entry = parent_.tlsCache().insertScope(this->scope_id_);
    tls_cache = &entry.text_readouts_;
    tls_rejected_stats = &entry.rejected_stats_;
  }

  const CentralCacheEntrySharedPtr& central_cache = centralCacheNoThreadAnalysis();
  return safeMakeStat<TextReadout>(
      final_stat_name, joiner.tagExtractedName(), stat_name_tags, central_cache->text_readouts_,
      fast_reject_result, central_cache->rejected_stats_,
      [](Allocator& allocator, StatName name, StatName tag_extracted_name,
         const StatNameTagVector& tags) -> TextReadoutSharedPtr {
        return allocator.makeTextReadout(name, tag_extracted_name, tags);
      },
      tls_cache, tls_rejected_stats, parent_.null_text_readout_);
}

CounterOptConstRef ThreadLocalStoreImpl::ScopeImpl::findCounter(StatName name) const {
  Thread::LockGuard lock(parent_.lock_);
  return findStatLockHeld<Counter>(name, central_cache_->counters_);
}

GaugeOptConstRef ThreadLocalStoreImpl::ScopeImpl::findGauge(StatName name) const {
  Thread::LockGuard lock(parent_.lock_);
  return findStatLockHeld<Gauge>(name, central_cache_->gauges_);
}

HistogramOptConstRef ThreadLocalStoreImpl::ScopeImpl::findHistogram(StatName name) const {
  Thread::LockGuard lock(parent_.lock_);
  return findHistogramLockHeld(name);
}

HistogramOptConstRef ThreadLocalStoreImpl::ScopeImpl::findHistogramLockHeld(StatName name) const
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(parent_.lock_) {
  auto iter = central_cache_->histograms_.find(name);
  if (iter == central_cache_->histograms_.end()) {
    return absl::nullopt;
  }

  RefcountPtr<Histogram> histogram_ref(iter->second);
  return std::cref(*histogram_ref);
}

TextReadoutOptConstRef ThreadLocalStoreImpl::ScopeImpl::findTextReadout(StatName name) const {
  Thread::LockGuard lock(parent_.lock_);
  return findStatLockHeld<TextReadout>(name, central_cache_->text_readouts_);
}

Histogram& ThreadLocalStoreImpl::tlsHistogram(ParentHistogramImpl& parent, uint64_t id) {
  // tlsHistogram() is generally not called for a histogram that is rejected by
  // the matcher, so no further rejection-checking is needed at this level.
  // TlsHistogram inherits its reject/accept status from ParentHistogram.

  // See comments in counterFromStatName() which explains the logic here.

  TlsHistogramSharedPtr* tls_histogram = nullptr;
  if (!shutting_down_ && tls_cache_) {
    tls_histogram = &(tlsCache().tls_histogram_cache_[id]);
    if (*tls_histogram != nullptr) {
      return **tls_histogram;
    }
  }

  StatNameTagHelper tag_helper(*this, parent.statName(), absl::nullopt);

  TlsHistogramSharedPtr hist_tls_ptr(
      new ThreadLocalHistogramImpl(parent.statName(), parent.unit(), tag_helper.tagExtractedName(),
                                   tag_helper.statNameTags(), symbolTable()));

  parent.addTlsHistogram(hist_tls_ptr);

  if (tls_histogram != nullptr) {
    *tls_histogram = hist_tls_ptr;
  }

  return *hist_tls_ptr;
}

ThreadLocalHistogramImpl::ThreadLocalHistogramImpl(StatName name, Histogram::Unit unit,
                                                   StatName tag_extracted_name,
                                                   const StatNameTagVector& stat_name_tags,
                                                   SymbolTable& symbol_table)
    : HistogramImplHelper(name, tag_extracted_name, stat_name_tags, symbol_table), unit_(unit),
      used_(false), created_thread_id_(std::this_thread::get_id()), symbol_table_(symbol_table) {
  histograms_[0] = hist_alloc();
  histograms_[1] = hist_alloc();
}

ThreadLocalHistogramImpl::~ThreadLocalHistogramImpl() {
  MetricImpl::clear(symbol_table_);
  hist_free(histograms_[0]);
  hist_free(histograms_[1]);
}

void ThreadLocalHistogramImpl::recordValue(uint64_t value) {
  ASSERT(std::this_thread::get_id() == created_thread_id_);
  hist_insert_intscale(histograms_[current_active_], value, 0, 1);
  used_ = true;
}

void ThreadLocalHistogramImpl::merge(histogram_t* target) {
  histogram_t** other_histogram = &histograms_[otherHistogramIndex()];
  hist_accumulate(target, other_histogram, 1);
  hist_clear(*other_histogram);
}

ParentHistogramImpl::ParentHistogramImpl(StatName name, Histogram::Unit unit,
                                         ThreadLocalStoreImpl& thread_local_store,
                                         StatName tag_extracted_name,
                                         const StatNameTagVector& stat_name_tags,
                                         ConstSupportedBuckets& supported_buckets, uint64_t id)
    : MetricImpl(name, tag_extracted_name, stat_name_tags, thread_local_store.symbolTable()),
      unit_(unit), thread_local_store_(thread_local_store), interval_histogram_(hist_alloc()),
      cumulative_histogram_(hist_alloc()),
      interval_statistics_(interval_histogram_, unit, supported_buckets),
      cumulative_statistics_(cumulative_histogram_, unit, supported_buckets), id_(id) {}

ParentHistogramImpl::~ParentHistogramImpl() {
  thread_local_store_.releaseHistogramCrossThread(id_);
  ASSERT(ref_count_ == 0);
  MetricImpl::clear(thread_local_store_.symbolTable());
  hist_free(interval_histogram_);
  hist_free(cumulative_histogram_);
}

void ParentHistogramImpl::incRefCount() { ++ref_count_; }

bool ParentHistogramImpl::decRefCount() {
  bool ret;
  if (shutting_down_) {
    // When shutting down, we cannot reference thread_local_store_, as
    // histograms can outlive the store. So we decrement the ref-count without
    // the stores' lock. We will not be removing the object from the store's
    // histogram map in this scenario, as the set was cleared during shutdown,
    // and will not be repopulated in histogramFromStatNameWithTags after
    // initiating shutdown.
    ret = --ref_count_ == 0;
  } else {
    // We delegate to the Store object to decrement the ref-count so it can hold
    // the lock to the map. If we don't hold a lock, another thread may
    // simultaneously try to allocate the same name'd histogram after we
    // decrement it, and we'll wind up with a dtor/update race. To avoid this we
    // must hold the lock until the histogram is removed from the map.
    //
    // See also StatsSharedImpl::decRefCount() in allocator_impl.cc, which has
    // the same issue.
    ret = thread_local_store_.decHistogramRefCount(*this, ref_count_);
  }
  return ret;
}

bool ThreadLocalStoreImpl::decHistogramRefCount(ParentHistogramImpl& hist,
                                                std::atomic<uint32_t>& ref_count) {
  // We must hold the store's histogram lock when decrementing the
  // refcount. Otherwise another thread may simultaneously try to allocate the
  // same name'd stat after we decrement it, and we'll wind up with a
  // dtor/update race. To avoid this we must hold the lock until the stat is
  // removed from the map.
  Thread::LockGuard lock(hist_mutex_);
  ASSERT(ref_count >= 1);
  if (--ref_count == 0) {
    if (!shutting_down_) {
      const size_t count = histogram_set_.erase(hist.statName());
      ASSERT(shutting_down_ || count == 1);
      sinked_histograms_.erase(&hist);
    }
    return true;
  }
  return false;
}

SymbolTable& ParentHistogramImpl::symbolTable() { return thread_local_store_.symbolTable(); }

Histogram::Unit ParentHistogramImpl::unit() const { return unit_; }

void ParentHistogramImpl::recordValue(uint64_t value) {
  Histogram& tls_histogram = thread_local_store_.tlsHistogram(*this, id_);
  tls_histogram.recordValue(value);
  thread_local_store_.deliverHistogramToSinks(*this, value);
}

bool ParentHistogramImpl::used() const {
  // Consider ParentHistogram used only if has ever been merged.
  return merged_;
}

bool ParentHistogramImpl::hidden() const { return false; }

void ParentHistogramImpl::merge() {
  Thread::ReleasableLockGuard lock(merge_lock_);
  if (merged_ || usedLockHeld()) {
    hist_clear(interval_histogram_);
    // Here we could copy all the pointers to TLS histograms in the tls_histogram_ list,
    // then release the lock before we do the actual merge. However it is not a big deal
    // because the tls_histogram merge is not that expensive as it is a single histogram
    // merge and adding TLS histograms is rare.
    for (const TlsHistogramSharedPtr& tls_histogram : tls_histograms_) {
      tls_histogram->merge(interval_histogram_);
    }
    // Since TLS merge is done, we can release the lock here.
    lock.release();
    hist_accumulate(cumulative_histogram_, &interval_histogram_, 1);
    cumulative_statistics_.refresh(cumulative_histogram_);
    interval_statistics_.refresh(interval_histogram_);
    merged_ = true;
  }
}

std::string ParentHistogramImpl::quantileSummary() const {
  if (used()) {
    std::vector<std::string> summary;
    const std::vector<double>& supported_quantiles_ref = interval_statistics_.supportedQuantiles();
    summary.reserve(supported_quantiles_ref.size());
    for (size_t i = 0; i < supported_quantiles_ref.size(); ++i) {
      summary.push_back(fmt::format("P{:g}({},{})", 100 * supported_quantiles_ref[i],
                                    interval_statistics_.computedQuantiles()[i],
                                    cumulative_statistics_.computedQuantiles()[i]));
    }
    return absl::StrJoin(summary, " ");
  } else {
    return {"No recorded values"};
  }
}

std::string ParentHistogramImpl::bucketSummary() const {
  if (used()) {
    std::vector<std::string> bucket_summary;
    ConstSupportedBuckets& supported_buckets = interval_statistics_.supportedBuckets();
    bucket_summary.reserve(supported_buckets.size());
    for (size_t i = 0; i < supported_buckets.size(); ++i) {
      bucket_summary.push_back(fmt::format("B{:g}({},{})", supported_buckets[i],
                                           interval_statistics_.computedBuckets()[i],
                                           cumulative_statistics_.computedBuckets()[i]));
    }
    return absl::StrJoin(bucket_summary, " ");
  } else {
    return {"No recorded values"};
  }
}

std::vector<Stats::ParentHistogram::Bucket>
ParentHistogramImpl::detailedlBucketsHelper(const histogram_t& histogram) {
  const uint32_t num_buckets = hist_num_buckets(&histogram);
  std::vector<Stats::ParentHistogram::Bucket> buckets(num_buckets);
  hist_bucket_t hist_bucket;
  for (uint32_t i = 0; i < num_buckets; ++i) {
    ParentHistogram::Bucket& bucket = buckets[i];
    hist_bucket_idx_bucket(&histogram, i, &hist_bucket, &bucket.count_);
    bucket.lower_bound_ = hist_bucket_to_double(hist_bucket);
    bucket.width_ = hist_bucket_to_double_bin_width(hist_bucket);
  }
  return buckets;
}

void ParentHistogramImpl::addTlsHistogram(const TlsHistogramSharedPtr& hist_ptr) {
  Thread::LockGuard lock(merge_lock_);
  tls_histograms_.emplace_back(hist_ptr);
}

bool ParentHistogramImpl::usedLockHeld() const {
  for (const TlsHistogramSharedPtr& tls_histogram : tls_histograms_) {
    if (tls_histogram->used()) {
      return true;
    }
  }
  return false;
}

void ThreadLocalStoreImpl::forEachCounter(SizeFn f_size, StatFn<Counter> f_stat) const {
  alloc_.forEachCounter(f_size, f_stat);
}

void ThreadLocalStoreImpl::forEachGauge(SizeFn f_size, StatFn<Gauge> f_stat) const {
  alloc_.forEachGauge(f_size, f_stat);
}

void ThreadLocalStoreImpl::forEachTextReadout(SizeFn f_size, StatFn<TextReadout> f_stat) const {
  alloc_.forEachTextReadout(f_size, f_stat);
}

void ThreadLocalStoreImpl::forEachHistogram(SizeFn f_size, StatFn<ParentHistogram> f_stat) const {
  Thread::LockGuard lock(hist_mutex_);
  if (f_size != nullptr) {
    f_size(histogram_set_.size());
  }
  for (ParentHistogramImpl* histogram : histogram_set_) {
    f_stat(*histogram);
  }
}

void ThreadLocalStoreImpl::forEachScope(std::function<void(std::size_t)> f_size,
                                        StatFn<const Scope> f_scope) const {
  std::vector<ScopeSharedPtr> scopes;
  iterateScopes([&scopes](const ScopeImplSharedPtr& scope) -> bool {
    scopes.push_back(scope);
    return true;
  });

  if (f_size != nullptr) {
    f_size(scopes.size());
  }
  for (const ScopeSharedPtr& scope : scopes) {
    f_scope(*scope);
  }
}

bool ThreadLocalStoreImpl::iterateScopesLockHeld(
    const std::function<bool(const ScopeImplSharedPtr&)> fn) const
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
  for (auto& iter : scopes_) {
    sync_.syncPoint(ThreadLocalStoreImpl::IterateScopeSync);

    // We keep the scopes as a map from Scope* to weak_ptr<Scope> so that if,
    // during the iteration, the last reference to a ScopeSharedPtr is dropped,
    // we can test for that here by attempting to lock the weak pointer, and
    // skip those that are nullptr.
    const std::weak_ptr<ScopeImpl>& scope = iter.second;
    const ScopeImplSharedPtr& locked = scope.lock();
    if (locked != nullptr && !fn(locked)) {
      return false;
    }
  }
  return true;
}

void ThreadLocalStoreImpl::forEachSinkedCounter(SizeFn f_size, StatFn<Counter> f_stat) const {
  alloc_.forEachSinkedCounter(f_size, f_stat);
}

void ThreadLocalStoreImpl::forEachSinkedGauge(SizeFn f_size, StatFn<Gauge> f_stat) const {
  alloc_.forEachSinkedGauge(f_size, f_stat);
}

void ThreadLocalStoreImpl::forEachSinkedTextReadout(SizeFn f_size,
                                                    StatFn<TextReadout> f_stat) const {
  alloc_.forEachSinkedTextReadout(f_size, f_stat);
}

void ThreadLocalStoreImpl::forEachSinkedHistogram(SizeFn f_size,
                                                  StatFn<ParentHistogram> f_stat) const {
  if (sink_predicates_.has_value() &&
      Runtime::runtimeFeatureEnabled("envoy.reloadable_features.enable_include_histograms")) {
    Thread::LockGuard lock(hist_mutex_);

    if (f_size != nullptr) {
      f_size(sinked_histograms_.size());
    }
    for (auto histogram : sinked_histograms_) {
      f_stat(*histogram);
    }
  } else {
    forEachHistogram(f_size, f_stat);
  }
}

void ThreadLocalStoreImpl::setSinkPredicates(std::unique_ptr<SinkPredicates>&& sink_predicates) {
  ASSERT(sink_predicates != nullptr);
  if (sink_predicates != nullptr) {
    sink_predicates_.emplace(*sink_predicates);
    alloc_.setSinkPredicates(std::move(sink_predicates));
    // Add histograms to the set of sinked histograms.
    Thread::LockGuard lock(hist_mutex_);
    sinked_histograms_.clear();
    for (auto& histogram : histogram_set_) {
      if (sink_predicates_->includeHistogram(*histogram)) {
        sinked_histograms_.insert(histogram);
      }
    }
  }
}

void ThreadLocalStoreImpl::extractAndAppendTags(StatName name, StatNamePool& pool,
                                                StatNameTagVector& stat_tags) {
  extractAndAppendTags(symbolTable().toString(name), pool, stat_tags);
}

void ThreadLocalStoreImpl::extractAndAppendTags(absl::string_view name, StatNamePool& pool,
                                                StatNameTagVector& stat_tags) {
  TagVector tags;
  tagProducer().produceTags(name, tags);
  for (const auto& tag : tags) {
    stat_tags.emplace_back(pool.add(tag.name_), pool.add(tag.value_));
  }
}

} // namespace Stats
} // namespace Envoy
