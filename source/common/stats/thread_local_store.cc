#include "common/stats/thread_local_store.h"

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <unordered_set>

#include "common/common/lock_guard.h"

namespace Envoy {
namespace Stats {

ThreadLocalStoreImpl::ThreadLocalStoreImpl(RawStatDataAllocator& alloc)
    : alloc_(alloc), default_scope_(createScope("")),
      tag_producer_(std::make_unique<TagProducerImpl>()),
      num_last_resort_stats_(default_scope_->counter("stats.overflow")), source_(*this) {}

ThreadLocalStoreImpl::~ThreadLocalStoreImpl() {
  ASSERT(shutting_down_);
  default_scope_.reset();
  ASSERT(scopes_.empty());
}

std::vector<CounterSharedPtr> ThreadLocalStoreImpl::counters() const {
  // Handle de-dup due to overlapping scopes.
  std::vector<CounterSharedPtr> ret;
  std::unordered_set<std::string> names;
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
  std::unordered_set<std::string> names;
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
  // Handle de-dup due to overlapping scopes.
  std::vector<ParentHistogramSharedPtr> ret;
  std::unordered_set<std::string> names;
  Thread::LockGuard lock(lock_);
  // TODO(ramaraochavali): As histograms don't share storage, there is a chance of duplicate names
  // here. We need to create global storage for histograms similar to how we have a central storage
  // in shared memory for counters/gauges. In the interim, no de-dup is done here. This may result
  // in histograms with duplicate names, but until shared storage is implementing it's ultimately
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
        [ this, scope_id = scope->scope_id_ ]()->void { clearScopeFromCaches(scope_id); });
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

ThreadLocalStoreImpl::SafeAllocData ThreadLocalStoreImpl::safeAlloc(const std::string& name) {
  RawStatData* data = alloc_.alloc(name);
  if (!data) {
    // If we run out of stat space from the allocator (which can happen if for example allocations
    // are coming from a fixed shared memory region, we need to deal with this case the best we
    // can. We must pass back the right allocator so that free() happens on the heap.
    num_last_resort_stats_.inc();
    return {*heap_allocator_.alloc(name), heap_allocator_};
  } else {
    return {*data, alloc_};
  }
}

std::atomic<uint64_t> ThreadLocalStoreImpl::ScopeImpl::next_scope_id_;

ThreadLocalStoreImpl::ScopeImpl::~ScopeImpl() { parent_.releaseScopeCrossThread(this); }

Counter& ThreadLocalStoreImpl::ScopeImpl::counter(const std::string& name) {
  // Determine the final name based on the prefix and the passed name.
  std::string final_name = prefix_ + name;

  // We now try to acquire a *reference* to the TLS cache shared pointer. This might remain null
  // if we don't have TLS initialized currently. The de-referenced pointer might be null if there
  // is no cache entry.
  CounterSharedPtr* tls_ref = nullptr;
  if (!parent_.shutting_down_ && parent_.tls_) {
    tls_ref =
        &parent_.tls_->getTyped<TlsCache>().scope_cache_[this->scope_id_].counters_[final_name];
  }

  // If we have a valid cache entry, return it.
  if (tls_ref && *tls_ref) {
    return **tls_ref;
  }

  // We must now look in the central store so we must be locked. We grab a reference to the
  // central store location. It might contain nothing. In this case, we allocate a new stat.
  Thread::LockGuard lock(parent_.lock_);
  CounterSharedPtr& central_ref = central_cache_.counters_[final_name];
  if (!central_ref) {
    SafeAllocData alloc = parent_.safeAlloc(final_name);
    std::vector<Tag> tags;
    std::string tag_extracted_name = parent_.getTagsForName(final_name, tags);
    central_ref.reset(
        new CounterImpl(alloc.data_, alloc.free_, std::move(tag_extracted_name), std::move(tags)));
  }

  // If we have a TLS location to store or allocation into, do it.
  if (tls_ref) {
    *tls_ref = central_ref;
  }

  // Finally we return the reference.
  return *central_ref;
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
  std::string final_name = prefix_ + name;
  GaugeSharedPtr* tls_ref = nullptr;
  if (!parent_.shutting_down_ && parent_.tls_) {
    tls_ref = &parent_.tls_->getTyped<TlsCache>().scope_cache_[this->scope_id_].gauges_[final_name];
  }

  if (tls_ref && *tls_ref) {
    return **tls_ref;
  }

  Thread::LockGuard lock(parent_.lock_);
  GaugeSharedPtr& central_ref = central_cache_.gauges_[final_name];
  if (!central_ref) {
    SafeAllocData alloc = parent_.safeAlloc(final_name);
    std::vector<Tag> tags;
    std::string tag_extracted_name = parent_.getTagsForName(final_name, tags);
    central_ref.reset(
        new GaugeImpl(alloc.data_, alloc.free_, std::move(tag_extracted_name), std::move(tags)));
  }

  if (tls_ref) {
    *tls_ref = central_ref;
  }

  return *central_ref;
}

Histogram& ThreadLocalStoreImpl::ScopeImpl::histogram(const std::string& name) {
  // See comments in counter(). There is no super clean way (via templates or otherwise) to
  // share this code so I'm leaving it largely duplicated for now.
  std::string final_name = prefix_ + name;
  ParentHistogramSharedPtr* tls_ref = nullptr;

  if (!parent_.shutting_down_ && parent_.tls_) {
    tls_ref = &parent_.tls_->getTyped<TlsCache>()
                   .scope_cache_[this->scope_id_]
                   .parent_histograms_[final_name];
  }

  if (tls_ref && *tls_ref) {
    return **tls_ref;
  }

  Thread::LockGuard lock(parent_.lock_);
  ParentHistogramImplSharedPtr& central_ref = central_cache_.histograms_[final_name];
  if (!central_ref) {
    std::vector<Tag> tags;
    std::string tag_extracted_name = parent_.getTagsForName(final_name, tags);
    central_ref.reset(new ParentHistogramImpl(final_name, parent_, *this,
                                              std::move(tag_extracted_name), std::move(tags)));
  }

  if (tls_ref) {
    *tls_ref = central_ref;
  }
  return *central_ref;
}

Histogram& ThreadLocalStoreImpl::ScopeImpl::tlsHistogram(const std::string& name,
                                                         ParentHistogramImpl& parent) {
  // See comments in counter() which explains the logic here.

  // Here prefix will not be considered because, by the time ParentHistogram calls this method
  // during recordValue, the prefix is already attached to the name.
  TlsHistogramSharedPtr* tls_ref = nullptr;
  if (!parent_.shutting_down_ && parent_.tls_) {
    tls_ref = &parent_.tls_->getTyped<TlsCache>().scope_cache_[this->scope_id_].histograms_[name];
  }

  if (tls_ref && *tls_ref) {
    return **tls_ref;
  }

  std::vector<Tag> tags;
  std::string tag_extracted_name = parent_.getTagsForName(name, tags);
  TlsHistogramSharedPtr hist_tls_ptr = std::make_shared<ThreadLocalHistogramImpl>(
      name, std::move(tag_extracted_name), std::move(tags));

  parent.addTlsHistogram(hist_tls_ptr);

  if (tls_ref) {
    *tls_ref = hist_tls_ptr;
  }
  return *hist_tls_ptr;
}

ThreadLocalHistogramImpl::ThreadLocalHistogramImpl(const std::string& name,
                                                   std::string&& tag_extracted_name,
                                                   std::vector<Tag>&& tags)
    : MetricImpl(name, std::move(tag_extracted_name), std::move(tags)), current_active_(0),
      flags_(0), created_thread_id_(std::this_thread::get_id()) {
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
    : MetricImpl(name, std::move(tag_extracted_name), std::move(tags)), parent_(parent),
      tls_scope_(tls_scope), interval_histogram_(hist_alloc()), cumulative_histogram_(hist_alloc()),
      interval_statistics_(interval_histogram_), cumulative_statistics_(cumulative_histogram_),
      merged_(false) {}

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
