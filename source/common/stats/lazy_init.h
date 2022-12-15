#pragma once

#include "source/common/common/thread.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Stats {

template <typename StatsStructType> class LazyableInterface {
public:
  // Helper operators to get-or-create and return the StatsStructType object.
  virtual StatsStructType* operator->() PURE;
  virtual StatsStructType& operator*() PURE;
  virtual ~LazyableInterface() {}
};

/**
 * Lazy-initialization wrapper for StatsStructType, intended for deferred instantiation of a block
 * of stats that might not be needed in a given Envoy process.
 *
 * This class is thread-safe -- instantiations can occur on multiple concurrent threads.
 */
template <typename StatsStructType> class LazyInit : public LazyableInterface<StatsStructType> {
public:
  // Capture the stat names object and the scope with a ctor, that can be used to instantiate a
  // StatsStructType object later.
  // Caller should make sure scope and stat_names outlive this object.
  LazyInit(const typename StatsStructType::StatNameType& stat_names, Stats::Scope& scope)
      : ctor_([&scope, &stat_names]() -> StatsStructType* {
          return new StatsStructType(stat_names, scope);
        }) {
    if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.enable_stats_lazyinit")) {
      internal_stats_.get(ctor_);
    }
  }
  // Helper operators to get-or-create and return the StatsStructType object.
  inline StatsStructType* operator->() override { return internal_stats_.get(ctor_); }
  inline StatsStructType& operator*() override { return *internal_stats_.get(ctor_); }

private:
  // TODO(stevenzzzz, jmarantz): Clean up this ctor_ by moving ownership to AtomicPtr, and drop it
  // when the nested object is created.
  std::function<StatsStructType*()> ctor_;
  Thread::AtomicPtr<StatsStructType, Thread::AtomicPtrAllocMode::DeleteOnDestruct>
      internal_stats_{};
};

// Non-LazyInit wrapper over StatsStructType.
template <typename StatsStructType> class DirectStats : public LazyableInterface<StatsStructType> {
public:
  DirectStats(const typename StatsStructType::StatNameType& stat_names, Stats::Scope& scope)
      : stats_(stat_names, scope) {}

  // Helper operators to get-or-create and return the StatsStructType object.
  inline StatsStructType* operator->() override { return &stats_; };
  inline StatsStructType& operator*() override { return stats_; };

private:
  StatsStructType stats_;
};

} // namespace Stats
} // namespace Envoy
