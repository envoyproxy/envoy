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
 * Log Linear Histogram implementation per thread.
 */
class ThreadLocalHistogramImpl : public Histogram, public MetricImpl {
public:
  ThreadLocalHistogramImpl(const std::string& name, Store& parent, std::string&& tag_extracted_name,
                           std::vector<Tag>&& tags)
      : MetricImpl(name, std::move(tag_extracted_name), std::move(tags)), parent_(parent),
        current_active_(0), flags_(0) {
    histograms_[0] = hist_alloc();
    histograms_[1] = hist_alloc();
  }

  ~ThreadLocalHistogramImpl() {
    hist_free(histograms_[0]);
    hist_free(histograms_[1]);
  }
  // Stats::Histogram
  void recordValue(uint64_t value) override {
    hist_insert_intscale(histograms_[current_active_], value, 0, 1);
    parent_.deliverHistogramToSinks(*this, value);
    flags_ |= Flags::Used;
  }

  void beginMerge() { current_active_ = 1 - current_active_; }

  // TODO: split the Histogram interface in to two - parent and tls.
  void merge() override { NOT_IMPLEMENTED; }

  bool used() const override { return flags_ & Flags::Used; }

  const HistogramStatistics& intervalStatistics() const override { return interval_statistics_; }

  const HistogramStatistics& cumulativeStatistics() const override {
    return cumulative_statistics_;
  }

  void merge(histogram_t* target) {
    histogram_t* hist_array[1];
    hist_array[0] = histograms_[1 - current_active_];
    hist_accumulate(target, hist_array, ARRAY_SIZE(hist_array));
    hist_clear(hist_array[0]);
  }

  Store& parent_;

private:
  uint64_t current_active_;
  histogram_t* histograms_[2];
  std::atomic<uint16_t> flags_;
  HistogramStatisticsImpl interval_statistics_;
  HistogramStatisticsImpl cumulative_statistics_;
};

typedef std::shared_ptr<ThreadLocalHistogramImpl> TlsHistogramSharedPtr;

/**
 * Log Linear Histogram implementation that is stored in the main thread.
 */
class HistogramParentImpl : public Histogram, public MetricImpl {
public:
  HistogramParentImpl(const std::string& name, Store& parent, std::string&& tag_extracted_name,
                      std::vector<Tag>&& tags)
      : MetricImpl(name, std::move(tag_extracted_name), std::move(tags)), parent_(parent) {
    interval_histogram_ = hist_alloc();
    cumulative_histogram_ = hist_alloc();
  }

  ~HistogramParentImpl() {
    hist_free(interval_histogram_);
    hist_free(cumulative_histogram_);
  }

  // Stats::Histogram
  // TODO: split the Histogram interface in to two - parent and tls.
  void recordValue(uint64_t) override { NOT_IMPLEMENTED; }

  bool used() const override {
    std::unique_lock<std::mutex> lock(merge_lock_);
    return usedWorker();
  }

  void addTlsHistogram(TlsHistogramSharedPtr hist_ptr) {
    std::unique_lock<std::mutex> lock(merge_lock_);
    tls_histograms_.emplace_back(hist_ptr);
  }

  /**
   * This method is called during the main stats flush process for each of the histogram. This
   * method iterates through the Tls histograms and collects the histogram data of all of them
   * in to "interval_histogram_". Then the collected "interval_histogram_" is merged to a
   * "cumulative_histogram". More details about threading model at
   * https://github.com/envoyproxy/envoy/issues/1965#issuecomment-376672282.
   */
  void merge() override {
    std::unique_lock<std::mutex> lock(merge_lock_);
    if (usedWorker()) {
      hist_clear(interval_histogram_);
      for (TlsHistogramSharedPtr tls_histogram : tls_histograms_) {
        tls_histogram->merge(interval_histogram_);
      }
      histogram_t* hist_array[1];
      hist_array[0] = interval_histogram_;
      hist_accumulate(cumulative_histogram_, hist_array, ARRAY_SIZE(hist_array));
      cumulative_statistics_ = HistogramStatisticsImpl(cumulative_histogram_);
      interval_statistics_ = HistogramStatisticsImpl(interval_histogram_);
    }
  }

  const HistogramStatistics& intervalStatistics() const override { return interval_statistics_; }

  const HistogramStatistics& cumulativeStatistics() const override {
    return cumulative_statistics_;
  }

  Store& parent_;
  std::list<TlsHistogramSharedPtr> tls_histograms_;

private:
  bool usedWorker() const {
    bool any_tls_used = false;
    for (TlsHistogramSharedPtr tls_histogram : tls_histograms_) {
      if (tls_histogram->used()) {
        any_tls_used = true;
        break;
      }
    }
    return any_tls_used;
  }

  histogram_t* interval_histogram_;
  histogram_t* cumulative_histogram_;
  HistogramStatisticsImpl interval_statistics_;
  HistogramStatisticsImpl cumulative_statistics_;
  mutable std::mutex merge_lock_;
};

typedef std::shared_ptr<HistogramParentImpl> ParentHistogramSharedPtr;

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
 */
class ThreadLocalStoreImpl : public StoreRoot {
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
  std::list<CounterSharedPtr> counters() const override;
  std::list<GaugeSharedPtr> gauges() const override;
  std::list<HistogramSharedPtr> histograms() const override;

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
  };

  struct CentralCacheEntry {
    std::unordered_map<std::string, CounterSharedPtr> counters_;
    std::unordered_map<std::string, GaugeSharedPtr> gauges_;
    std::unordered_map<std::string, ParentHistogramSharedPtr> histograms_;
  };

  struct ScopeImpl : public Scope {
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
  Counter& num_last_resort_stats_;
  HeapRawStatDataAllocator heap_allocator_;
};

} // namespace Stats
} // namespace Envoy
