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

  // Stats::StoreRoot
  void addSink(Sink& sink) override { timer_sinks_.push_back(sink); }
  void setTagProducer(TagProducerPtr&& tag_producer) override {
    tag_producer_ = std::move(tag_producer);
  }
  void initializeThreading(Event::Dispatcher& main_thread_dispatcher,
                           ThreadLocal::Instance& tls) override;
  void shutdownThreading() override;

private:
  struct TlsCacheEntry {
    std::unordered_map<std::string, CounterSharedPtr> counters_;
    std::unordered_map<std::string, GaugeSharedPtr> gauges_;
    std::unordered_map<std::string, HistogramSharedPtr> histograms_;
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
    TlsCacheEntry central_cache_;
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
