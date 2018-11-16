#include "common/stats/thread_local_store.h"

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/stats/histogram.h"
#include "envoy/stats/sink.h"
#include "envoy/stats/stat_data_allocator.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_options.h"

#include "common/common/lock_guard.h"
#include "common/stats/stats_matcher_impl.h"
#include "common/stats/tag_producer_impl.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Stats {

ThreadLocalStoreImpl::ThreadLocalStoreImpl(const StatsOptions& stats_options,
                                           StatDataAllocator& alloc)
    : stats_options_(stats_options), alloc_(alloc), default_scope_(createScope("")),
      tag_producer_(std::make_unique<TagProducerImpl>()),
      stats_matcher_(std::make_unique<StatsMatcherImpl>()),
      num_last_resort_stats_(default_scope_->counter("stats.overflow")), source_(*this) {}

ThreadLocalStoreImpl::~ThreadLocalStoreImpl() {
  ASSERT(shutting_down_);
  default_scope_.reset();
  ASSERT(scopes_.empty());
}

std::vector<CounterSharedPtr> ThreadLocalStoreImpl::counters() const {
  // Handle de-dup due to overlapping scopes.
  std::vector<CounterSharedPtr> ret;
  CharStarHashSet names;
  Thread::LockGuard lock(lock_);
  for (ScopeImpl* scope : scopes_) {
    for (auto& counter : scope->central_cache_.counters_) {
      if (names.insert(counter.first).second) {
        ret.push_back(counter.second);
      }
    }
  }

  return ret;
}

ScopePtr ThreadLocalStoreImpl::createScope(const std::string& name) {
  std::unique_ptr<ScopeImpl> new_scope(new ScopeImpl(*this, name));
  Thread::LockGuard lock(lock_);
  scopes_.emplace(new_scope.get());
  return std::move(new_scope);
}

std::vector<GaugeSharedPtr> ThreadLocalStoreImpl::gauges() const {
  // Handle de-dup due to overlapping scopes.
  std::vector<GaugeSharedPtr> ret;
  CharStarHashSet names;
  Thread::LockGuard lock(lock_);
  for (ScopeImpl* scope : scopes_) {
    for (auto& gauge : scope->central_cache_.gauges_) {
      if (names.insert(gauge.first).second) {
        ret.push_back(gauge.second);
      }
    }
  }

  return ret;
}

std::vector<ParentHistogramSharedPtr> ThreadLocalStoreImpl::histograms() const {
  std::vector<ParentHistogramSharedPtr> ret;
  Thread::LockGuard lock(lock_);
  // TODO(ramaraochavali): As histograms don't share storage, there is a chance of duplicate names
  // here. We need to create global storage for histograms similar to how we have a central storage
  // in shared memory for counters/gauges. In the interim, no de-dup is done here. This may result
  // in histograms with duplicate names, but until shared storage is implemented it's ultimately
  // less confusing for users who have such configs.
  for (ScopeImpl* scope : scopes_) {
    for (const auto& name_histogram_pair : scope->central_cache_.histograms_) {
      const ParentHistogramSharedPtr& parent_hist = name_histogram_pair.second;
      ret.push_back(parent_hist);
    }
  }

  return ret;
}

void ThreadLocalStoreImpl::initializeThreading(Event::Dispatcher& main_thread_dispatcher,
                                               ThreadLocal::Instance& tls) {
  main_thread_dispatcher_ = &main_thread_dispatcher;
  tls_ = tls.allocateSlot();
  tls_->set([](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return std::make_shared<TlsCache>();
  });
}

void ThreadLocalStoreImpl::shutdownThreading() {
  // This will block both future cache fills as well as cache flushes.
  shutting_down_ = true;
}

void ThreadLocalStoreImpl::mergeHistograms(PostMergeCb merge_complete_cb) {
  if (!shutting_down_) {
    ASSERT(!merge_in_progress_);
    merge_in_progress_ = true;
    tls_->runOnAllThreads(
        [this]() -> void {
          for (const auto& scope : tls_->getTyped<TlsCache>().scope_cache_) {
            const TlsCacheEntry& tls_cache_entry = scope.second;
            for (const auto& name_histogram_pair : tls_cache_entry.histograms_) {
              const TlsHistogramSharedPtr& tls_hist = name_histogram_pair.second;
              tls_hist->beginMerge();
            }
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
    for (const ParentHistogramSharedPtr& histogram : histograms()) {
      histogram->merge();
    }
    merge_complete_cb();
    merge_in_progress_ = false;
  }
}

void ThreadLocalStoreImpl::releaseScopeCrossThread(ScopeImpl* scope) {
  Thread::LockGuard lock(lock_);
  ASSERT(scopes_.count(scope) == 1);
  scopes_.erase(scope);

  // This can happen from any thread. We post() back to the main thread which will initiate the
  // cache flush operation.
  if (!shutting_down_ && main_thread_dispatcher_) {
    main_thread_dispatcher_->post(
        [this, scope_id = scope->scope_id_]() -> void { clearScopeFromCaches(scope_id); });
  }
}

std::string ThreadLocalStoreImpl::getTagsForName(const std::string& name,
                                                 std::vector<Tag>& tags) const {
  return tag_producer_->produceTags(name, tags);
}

void ThreadLocalStoreImpl::clearScopeFromCaches(uint64_t scope_id) {
  // If we are shutting down we no longer perform cache flushes as workers may be shutting down
  // at the same time.
  if (!shutting_down_) {
    // Perform a cache flush on all threads.
    tls_->runOnAllThreads(
        [this, scope_id]() -> void { tls_->getTyped<TlsCache>().scope_cache_.erase(scope_id); });
  }
}

absl::string_view ThreadLocalStoreImpl::truncateStatNameIfNeeded(absl::string_view name) {
  // If the main allocator requires stat name truncation, do so now, though any
  // warnings will be printed only if the truncated stat requires a new
  // allocation.
  if (alloc_.requiresBoundedStatNameSize()) {
    const uint64_t max_length = stats_options_.maxNameLength();
    name = name.substr(0, max_length);
  }
  return name;
}

std::atomic<uint64_t> ThreadLocalStoreImpl::ScopeImpl::next_scope_id_;

ThreadLocalStoreImpl::ScopeImpl::~ScopeImpl() { parent_.releaseScopeCrossThread(this); }

template <class StatType>
StatType& ThreadLocalStoreImpl::ScopeImpl::safeMakeStat(
    const std::string& name, StatMap<std::shared_ptr<StatType>>& central_cache_map,
    MakeStatFn<StatType> make_stat, StatMap<std::shared_ptr<StatType>>* tls_cache) {

  const char* stat_key = name.c_str();
  std::unique_ptr<std::string> truncation_buffer;
  absl::string_view truncated_name = parent_.truncateStatNameIfNeeded(name);
  if (truncated_name.size() < name.size()) {
    truncation_buffer = std::make_unique<std::string>(std::string(truncated_name));
    stat_key = truncation_buffer->c_str(); // must be nul-terminated.
  }

  // If we have a valid cache entry, return it.
  if (tls_cache) {
    auto pos = tls_cache->find(stat_key);
    if (pos != tls_cache->end()) {
      return *pos->second;
    }
  }

  // We must now look in the central store so we must be locked. We grab a reference to the
  // central store location. It might contain nothing. In this case, we allocate a new stat.
  Thread::LockGuard lock(parent_.lock_);
  auto p = central_cache_map.find(stat_key);
  std::shared_ptr<StatType>* central_ref = nullptr;
  if (p != central_cache_map.end()) {
    central_ref = &(p->second);
  } else {
    // If we had to truncate, warn now that we've missed all caches.
    if (truncation_buffer != nullptr) {
      ENVOY_LOG_MISC(
          warn,
          "Statistic '{}' is too long with {} characters, it will be truncated to {} characters",
          name, name.size(), truncation_buffer->size());
    }

    std::vector<Tag> tags;

    // Tag extraction occurs on the original, untruncated name so the extraction
    // can complete properly, even if the tag values are partially truncated.
    std::string tag_extracted_name = parent_.getTagsForName(name, tags);
    std::shared_ptr<StatType> stat =
        make_stat(parent_.alloc_, truncated_name, std::move(tag_extracted_name), std::move(tags));
    if (stat == nullptr) {
      // TODO(jmarantz): If make_stat fails, the actual move does not actually occur
      // for tag_extracted_name and tags, so there is no use-after-move problem.
      // In order to increase the readability of the code, refactoring is done here.
      parent_.num_last_resort_stats_.inc();
      stat = make_stat(parent_.heap_allocator_, truncated_name,
                       std::move(tag_extracted_name), // NOLINT(bugprone-use-after-move)
                       std::move(tags));              // NOLINT(bugprone-use-after-move)
      ASSERT(stat != nullptr);
    }
    central_ref = &central_cache_map[stat->nameCStr()];
    *central_ref = stat;
  }

  // If we have a TLS cache, insert the stat.
  if (tls_cache) {
    tls_cache->insert(std::make_pair((*central_ref)->nameCStr(), *central_ref));
  }

  // Finally we return the reference.
  return **central_ref;
}

Counter& ThreadLocalStoreImpl::ScopeImpl::counter(const std::string& name) {
  // Determine the final name based on the prefix and the passed name.
  //
  // Note that we can do map.find(final_name.c_str()), but we cannot do
  // map[final_name.c_str()] as the char*-keyed maps would then save the pointer
  // to a temporary, and address sanitization errors would follow. Instead we
  // must do a find() first, using the value if it succeeds. If it fails, then
  // after we construct the stat we can insert it into the required maps. This
  // strategy costs an extra hash lookup for each miss, but saves time
  // re-copying the string and significant memory overhead.
  std::string final_name = prefix_ + name;

  // TODO(ambuc): If stats_matcher_ depends on regexes, this operation (on the hot path) could
  // become prohibitively expensive. Revisit this usage in the future.
  if (parent_.stats_matcher_->rejects(final_name)) {
    return null_counter_;
  }

  // We now find the TLS cache. This might remain null if we don't have TLS
  // initialized currently.
  StatMap<CounterSharedPtr>* tls_cache = nullptr;
  if (!parent_.shutting_down_ && parent_.tls_) {
    tls_cache = &parent_.tls_->getTyped<TlsCache>().scope_cache_[this->scope_id_].counters_;
  }

  return safeMakeStat<Counter>(
      final_name, central_cache_.counters_,
      [](StatDataAllocator& allocator, absl::string_view name, std::string&& tag_extracted_name,
         std::vector<Tag>&& tags) -> CounterSharedPtr {
        return allocator.makeCounter(name, std::move(tag_extracted_name), std::move(tags));
      },
      tls_cache);
}

void ThreadLocalStoreImpl::ScopeImpl::deliverHistogramToSinks(const Histogram& histogram,
                                                              uint64_t value) {
  // Thread local deliveries must be blocked outright for histograms and timers during shutdown.
  // This is because the sinks may end up trying to create new connections via the thread local
  // cluster manager which may already be destroyed (there is no way to sequence this because the
  // cluster manager destroying can create deliveries). We special case this explicitly to avoid
  // having to implement a shutdown() method (or similar) on every TLS object.
  if (parent_.shutting_down_) {
    return;
  }

  for (Sink& sink : parent_.timer_sinks_) {
    sink.onHistogramComplete(histogram, value);
  }
}

Gauge& ThreadLocalStoreImpl::ScopeImpl::gauge(const std::string& name) {
  // See comments in counter(). There is no super clean way (via templates or otherwise) to
  // share this code so I'm leaving it largely duplicated for now.
  //
  // Note that we can do map.find(final_name.c_str()), but we cannot do
  // map[final_name.c_str()] as the char*-keyed maps would then save the pointer to
  // a temporary, and address sanitization errors would follow. Instead we must
  // do a find() first, using tha if it succeeds. If it fails, then after we
  // construct the stat we can insert it into the required maps.
  std::string final_name = prefix_ + name;

  // See warning/comments in counter().
  if (parent_.stats_matcher_->rejects(final_name)) {
    return null_gauge_;
  }

  StatMap<GaugeSharedPtr>* tls_cache = nullptr;
  if (!parent_.shutting_down_ && parent_.tls_) {
    tls_cache = &parent_.tls_->getTyped<TlsCache>().scope_cache_[this->scope_id_].gauges_;
  }

  return safeMakeStat<Gauge>(
      final_name, central_cache_.gauges_,
      [](StatDataAllocator& allocator, absl::string_view name, std::string&& tag_extracted_name,
         std::vector<Tag>&& tags) -> GaugeSharedPtr {
        return allocator.makeGauge(name, std::move(tag_extracted_name), std::move(tags));
      },
      tls_cache);
}

Histogram& ThreadLocalStoreImpl::ScopeImpl::histogram(const std::string& name) {
  // See comments in counter(). There is no super clean way (via templates or otherwise) to
  // share this code so I'm leaving it largely duplicated for now.
  //
  // Note that we can do map.find(final_name.c_str()), but we cannot do
  // map[final_name.c_str()] as the char*-keyed maps would then save the pointer to
  // a temporary, and address sanitization errors would follow. Instead we must
  // do a find() first, using tha if it succeeds. If it fails, then after we
  // construct the stat we can insert it into the required maps.
  std::string final_name = prefix_ + name;

  // See warning/comments in counter().
  if (parent_.stats_matcher_->rejects(final_name)) {
    return null_histogram_;
  }

  StatMap<ParentHistogramSharedPtr>* tls_cache = nullptr;
  if (!parent_.shutting_down_ && parent_.tls_) {
    tls_cache =
        &parent_.tls_->getTyped<TlsCache>().scope_cache_[this->scope_id_].parent_histograms_;
    auto p = tls_cache->find(final_name.c_str());
    if (p != tls_cache->end()) {
      return *p->second;
    }
  }

  Thread::LockGuard lock(parent_.lock_);
  auto p = central_cache_.histograms_.find(final_name.c_str());
  ParentHistogramImplSharedPtr* central_ref = nullptr;
  if (p != central_cache_.histograms_.end()) {
    central_ref = &p->second;
  } else {
    std::vector<Tag> tags;
    std::string tag_extracted_name = parent_.getTagsForName(final_name, tags);
    auto stat = std::make_shared<ParentHistogramImpl>(
        final_name, parent_, *this, std::move(tag_extracted_name), std::move(tags));
    central_ref = &central_cache_.histograms_[stat->nameCStr()];
    *central_ref = stat;
  }

  if (tls_cache != nullptr) {
    tls_cache->insert(std::make_pair((*central_ref)->nameCStr(), *central_ref));
  }
  return **central_ref;
}

Histogram& ThreadLocalStoreImpl::ScopeImpl::tlsHistogram(const std::string& name,
                                                         ParentHistogramImpl& parent) {
  if (parent_.stats_matcher_->rejects(name)) {
    return null_histogram_;
  }

  // See comments in counter() which explains the logic here.

  StatMap<TlsHistogramSharedPtr>* tls_cache = nullptr;
  if (!parent_.shutting_down_ && parent_.tls_) {
    tls_cache = &parent_.tls_->getTyped<TlsCache>().scope_cache_[this->scope_id_].histograms_;
    auto p = tls_cache->find(name.c_str());
    if (p != tls_cache->end()) {
      return *p->second;
    }
  }

  std::vector<Tag> tags;
  std::string tag_extracted_name = parent_.getTagsForName(name, tags);
  TlsHistogramSharedPtr hist_tls_ptr = std::make_shared<ThreadLocalHistogramImpl>(
      name, std::move(tag_extracted_name), std::move(tags));

  parent.addTlsHistogram(hist_tls_ptr);

  if (tls_cache) {
    tls_cache->insert(std::make_pair(hist_tls_ptr->nameCStr(), hist_tls_ptr));
  }
  return *hist_tls_ptr;
}

ThreadLocalHistogramImpl::ThreadLocalHistogramImpl(const std::string& name,
                                                   std::string&& tag_extracted_name,
                                                   std::vector<Tag>&& tags)
    : MetricImpl(std::move(tag_extracted_name), std::move(tags)), current_active_(0), flags_(0),
      created_thread_id_(std::this_thread::get_id()), name_(name) {
  histograms_[0] = hist_alloc();
  histograms_[1] = hist_alloc();
}

ThreadLocalHistogramImpl::~ThreadLocalHistogramImpl() {
  hist_free(histograms_[0]);
  hist_free(histograms_[1]);
}

void ThreadLocalHistogramImpl::recordValue(uint64_t value) {
  ASSERT(std::this_thread::get_id() == created_thread_id_);
  hist_insert_intscale(histograms_[current_active_], value, 0, 1);
  flags_ |= Flags::Used;
}

void ThreadLocalHistogramImpl::merge(histogram_t* target) {
  histogram_t** other_histogram = &histograms_[otherHistogramIndex()];
  hist_accumulate(target, other_histogram, 1);
  hist_clear(*other_histogram);
}

ParentHistogramImpl::ParentHistogramImpl(const std::string& name, Store& parent,
                                         TlsScope& tls_scope, std::string&& tag_extracted_name,
                                         std::vector<Tag>&& tags)
    : MetricImpl(std::move(tag_extracted_name), std::move(tags)), parent_(parent),
      tls_scope_(tls_scope), interval_histogram_(hist_alloc()), cumulative_histogram_(hist_alloc()),
      interval_statistics_(interval_histogram_), cumulative_statistics_(cumulative_histogram_),
      merged_(false), name_(name) {}

ParentHistogramImpl::~ParentHistogramImpl() {
  hist_free(interval_histogram_);
  hist_free(cumulative_histogram_);
}

void ParentHistogramImpl::recordValue(uint64_t value) {
  Histogram& tls_histogram = tls_scope_.tlsHistogram(name(), *this);
  tls_histogram.recordValue(value);
  parent_.deliverHistogramToSinks(*this, value);
}

bool ParentHistogramImpl::used() const {
  // Consider ParentHistogram used only if has ever been merged.
  return merged_;
}

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

const std::string ParentHistogramImpl::summary() const {
  if (used()) {
    std::vector<std::string> summary;
    const std::vector<double>& supported_quantiles_ref = interval_statistics_.supportedQuantiles();
    summary.reserve(supported_quantiles_ref.size());
    for (size_t i = 0; i < supported_quantiles_ref.size(); ++i) {
      summary.push_back(fmt::format("P{}({},{})", 100 * supported_quantiles_ref[i],
                                    interval_statistics_.computedQuantiles()[i],
                                    cumulative_statistics_.computedQuantiles()[i]));
    }
    return absl::StrJoin(summary, " ");
  } else {
    return std::string("No recorded values");
  }
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

} // namespace Stats
} // namespace Envoy
