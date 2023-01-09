#pragma once

#include "source/common/common/thread.h"

namespace Envoy {
namespace Stats {

/**
 * Interface for stats lazy initialization.
 * To reduce memory and CPU consumption, Envoy can enable the bootstrap config
 * :ref:`enable_lazyinit_stats
 * <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.enable_lazyinit_stats>`.
 * A 'StatsStructType' is only created when any of its field is referenced.
 * See more context: https://github.com/envoyproxy/envoy/issues/23575
 */
template <typename StatsStructType> class LazyCompatibleInterface {
public:
  // Helper operators to get-or-create and return the StatsStructType object.
  virtual StatsStructType* operator->() PURE;
  virtual StatsStructType& operator*() PURE;
  virtual ~LazyCompatibleInterface() = default;
};

/**
 * Lazy-initialization wrapper for StatsStructType, intended for deferred instantiation of a block
 * of stats that might not be needed in a given Envoy process.
 *
 * This class is thread-safe -- instantiations can occur on multiple concurrent threads.
 */
template <typename StatsStructType>
class LazyInit : public LazyCompatibleInterface<StatsStructType> {
public:
  // Capture the stat names object and the scope with a ctor, that can be used to instantiate a
  // StatsStructType object later.
  // Caller should make sure scope and stat_names outlive this object.
  LazyInit(const typename StatsStructType::StatNameType& stat_names, Stats::ScopeSharedPtr scope)
      : inited_(Stats::Utility::gaugeFromElements(
            *scope, {Stats::DynamicName(StatsStructType::typeName()), Stats::DynamicName("inited")},
            Stats::Gauge::ImportMode::Accumulate)),
        ctor_([scope = std::move(scope), &stat_names, this]() -> StatsStructType* {
          inited_.inc();
          return new StatsStructType(stat_names, *scope);
        }) {
    if (inited_.value() > 0) {
      // instantiate();
    }
  }
  // Helper operators to get-or-create and return the StatsStructType object.
  inline StatsStructType* operator->() override { return instantiate(); }
  inline StatsStructType& operator*() override { return *instantiate(); }
  ~LazyInit() {
    if (inited_.value() > 0) {
      inited_.dec();
    }
  }

private:
  inline StatsStructType* instantiate() { return internal_stats_.get(ctor_); }
  // For
  Gauge& inited_;
  // TODO(stevenzzzz, jmarantz): Clean up this ctor_ by moving ownership to AtomicPtr, and drop it
  // when the nested object is created.
  std::function<StatsStructType*()> ctor_;
  Thread::AtomicPtr<StatsStructType, Thread::AtomicPtrAllocMode::DeleteOnDestruct>
      internal_stats_{};
};

// Non-LazyInit wrapper over StatsStructType.
template <typename StatsStructType>
class DirectStats : public LazyCompatibleInterface<StatsStructType> {
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
