#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "envoy/thread_local/thread_local.h"

#include "common/stats/stats_impl.h"

namespace Envoy {
namespace Stats {

/**
 * A histogram that is stored in TLS and used to record values per thread. This holds two
 * histograms, one to collect the values and other as backup that is used for merge process. The
 * swap happens during the merge process.
 */
class ThreadLocalHistogramImpl : public Histogram, public MetricImpl {
public:
  ThreadLocalHistogramImpl(const std::string& name, std::string&& tag_extracted_name,
                           std::vector<Tag>&& tags);

  ~ThreadLocalHistogramImpl();

  void merge(histogram_t* target);

  /**
   * Called in the beginning of merge process. Swaps the histogram used for collection so that we do
   * not have to lock the histogram in high throughput TLS writes.
   */
  void beginMerge() {
    // This switches the current_active_ between 1 and 0.
    current_active_ = otherHistogramIndex();
  }

  // Stats::Histogram
  void recordValue(uint64_t value) override;
  bool used() const override { return flags_ & Flags::Used; }

private:
  uint64_t otherHistogramIndex() const { return 1 - current_active_; }
  uint64_t current_active_;
  histogram_t* histograms_[2];
  std::atomic<uint16_t> flags_;
  std::thread::id created_thread_id_;
};

typedef std::shared_ptr<ThreadLocalHistogramImpl> TlsHistogramSharedPtr;

class TlsScope;

/**
 * Log Linear Histogram implementation that is stored in the main thread.
 */
class ParentHistogramImpl : public ParentHistogram, public MetricImpl {
public:
  ParentHistogramImpl(const std::string& name, Store& parent, TlsScope& tlsScope,
                      std::string&& tag_extracted_name, std::vector<Tag>&& tags);

  virtual ~ParentHistogramImpl();

  void addTlsHistogram(const TlsHistogramSharedPtr& hist_ptr);

  bool used() const override;
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

private:
  bool usedLockHeld() const;

  Store& parent_;
  TlsScope& tls_scope_;
  histogram_t* interval_histogram_;
  histogram_t* cumulative_histogram_;
  HistogramStatisticsImpl interval_statistics_;
  HistogramStatisticsImpl cumulative_statistics_;
  mutable std::mutex merge_lock_;
  std::list<TlsHistogramSharedPtr> tls_histograms_;
};

typedef std::shared_ptr<ParentHistogramImpl> ParentHistogramImplSharedPtr;

/**
 * Class used to create ThreadLocalHistogram in the scope.
 */
class TlsScope : public Scope {
public:
  virtual ~TlsScope() {}

  // TODO(ramaraochavali): Allow direct TLS access for the advanced consumers.
  /**
   * @return a ThreadLocalHistogram within the scope's namespace.
   * @param name name of the histogram with scope prefix attached.
   */
  virtual Histogram& tlsHistogram(const std::string& name, ParentHistogramImpl& parent) PURE;
};

/**
 * Store implementation with thread local caching. This implementation supports the following
 * features:
 * - Thread local per scope stat caching.
 * - Overallaping scopes with proper reference counting (2 scopes with the same name will point to
 *   the same backing stats).
 * - Scope deletion.
 *
 * This implementation is complicated so here is a rough overview of the threading model.
 * - The store can be used before threading is initialized. This is needed during server init.
 * - Scopes can be created from any thread, though in practice they are only created from the main
 *   thread.
 * - Scopes can be deleted from any thread, and they are in practice as scopes are likely to be
 *   shared across all worker threads.
 * - Per thread caches are checked, and if empty, they are populated from the central cache.
 * - Scopes are entirely owned by the caller. The store only keeps weak pointers.
 * - When a scope is destroyed, a cache flush operation is run on all threads to flush any cached
 *   data owned by the destroyed scope.
 * - NOTE: It is theoretically possible that when a scope is deleted, it could be reallocated
 *         with the same address, and a cache flush operation could race and delete cache data
 *         for the new scope. This is extremely unlikely, and if it happens the cache will be
 *         repopulated on the next access.
 * - Since it's possible to have overlapping scopes, we de-dup stats when counters() or gauges() is
 *   called since these are very uncommon operations.
 * - Though this implementation is designed to work with a fixed shared memory space, it will fall
 *   back to heap allocated stats if needed. NOTE: In this case, overlapping scopes will not share
 *   the same backing store. This is to keep things simple, it could be done in the future if
 *   needed.
 *
 * The threading model for managing histograms is as described below.
 * Each Histogram implementation will have 2 parts.
 *  - "main" thread parent which is called "ParentHistogram".
 *  - "per-thread" collector which is called "ThreadLocalHistogram".
 * Worker threads will write to ParentHistogram which checks whether a TLS histogram is available.
 * If there is one it will write to it, otherwise creates new one and writes to it.
 * During the flush process the following sequence is followed.
 *  - The main thread starts the flush process by posting a message to every worker which tells the
 *    worker to swap its "active" histogram with its "backup" histogram. This is acheived via a call
 *    to "beginMerge" method.
 *  - Each TLS histogram has 2 histograms it makes use of, swapping back and forth. It manages a
 *    current_active index via which it writes to the correct histogram.
 *  - When all workers have done, the main thread continues with the flush process where the
 *    "actual" merging happens.
 *  - As the active histograms are swapped in TLS histograms, on the main thread, we can be sure
 *    that no worker is writing into the "backup" histogram.
 *  - The main thread now goes through all histograms, collect them across each worker and
 *    accumulates in to "interval" histograms.
 *  - Finally the main "interval" histogram is merged to "cumulative" histogram.
 */
class ThreadLocalStoreImpl : Logger::Loggable<Logger::Id::stats>, public StoreRoot {
public:
  ThreadLocalStoreImpl(RawStatDataAllocator& alloc);
  ~ThreadLocalStoreImpl();

  // Stats::Scope
  Counter& counter(const std::string& name) override { return default_scope_->counter(name); }
  ScopePtr createScope(const std::string& name) override;
  void deliverHistogramToSinks(const Histogram& histogram, uint64_t value) override {
    return default_scope_->deliverHistogramToSinks(histogram, value);
  }
  Gauge& gauge(const std::string& name) override { return default_scope_->gauge(name); }
  Histogram& histogram(const std::string& name) override {
    return default_scope_->histogram(name);
  };

  // Stats::Store
  // TODO(ramaraochavali): Consider changing the implementation of these methods to use vectors and
  // use std::sort, rather than inserting into a map and pulling it out for better performance.
  std::list<CounterSharedPtr> counters() const override;
  std::list<GaugeSharedPtr> gauges() const override;
  std::list<ParentHistogramSharedPtr> histograms() const override;

  // Stats::StoreRoot
  void addSink(Sink& sink) override { timer_sinks_.push_back(sink); }
  void setTagProducer(TagProducerPtr&& tag_producer) override {
    tag_producer_ = std::move(tag_producer);
  }
  void initializeThreading(Event::Dispatcher& main_thread_dispatcher,
                           ThreadLocal::Instance& tls) override;
  void shutdownThreading() override;

  void mergeHistograms(PostMergeCb mergeCb) override;

private:
  struct TlsCacheEntry {
    std::unordered_map<std::string, CounterSharedPtr> counters_;
    std::unordered_map<std::string, GaugeSharedPtr> gauges_;
    std::unordered_map<std::string, TlsHistogramSharedPtr> histograms_;
    std::unordered_map<std::string, ParentHistogramSharedPtr> parent_histograms_;
  };

  struct CentralCacheEntry {
    std::unordered_map<std::string, CounterSharedPtr> counters_;
    std::unordered_map<std::string, GaugeSharedPtr> gauges_;
    std::unordered_map<std::string, ParentHistogramImplSharedPtr> histograms_;
  };

  struct ScopeImpl : public TlsScope {
    ScopeImpl(ThreadLocalStoreImpl& parent, const std::string& prefix)
        : parent_(parent), prefix_(Utility::sanitizeStatsName(prefix)) {}
    ~ScopeImpl();

    // Stats::Scope
    Counter& counter(const std::string& name) override;
    ScopePtr createScope(const std::string& name) override {
      return parent_.createScope(prefix_ + name);
    }
    void deliverHistogramToSinks(const Histogram& histogram, uint64_t value) override;
    Gauge& gauge(const std::string& name) override;
    Histogram& histogram(const std::string& name) override;
    Histogram& tlsHistogram(const std::string& name, ParentHistogramImpl& parent) override;

    ThreadLocalStoreImpl& parent_;
    const std::string prefix_;
    CentralCacheEntry central_cache_;
  };

  struct TlsCache : public ThreadLocal::ThreadLocalObject {
    std::unordered_map<ScopeImpl*, TlsCacheEntry> scope_cache_;
  };

  struct SafeAllocData {
    RawStatData& data_;
    RawStatDataAllocator& free_;
  };

  std::string getTagsForName(const std::string& name, std::vector<Tag>& tags);
  void clearScopeFromCaches(ScopeImpl* scope);
  void releaseScopeCrossThread(ScopeImpl* scope);
  SafeAllocData safeAlloc(const std::string& name);
  void mergeInternal(PostMergeCb mergeCb);

  RawStatDataAllocator& alloc_;
  Event::Dispatcher* main_thread_dispatcher_{};
  ThreadLocal::SlotPtr tls_;
  mutable std::mutex lock_;
  std::unordered_set<ScopeImpl*> scopes_;
  ScopePtr default_scope_;
  std::list<std::reference_wrapper<Sink>> timer_sinks_;
  TagProducerPtr tag_producer_;
  std::atomic<bool> shutting_down_{};
  std::atomic<bool> merge_in_progress_{};
  Counter& num_last_resort_stats_;
  HeapRawStatDataAllocator heap_allocator_;
};

} // namespace Stats
} // namespace Envoy
