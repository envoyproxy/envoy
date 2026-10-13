#pragma once

#include "envoy/common/pure.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"

#include "source/common/common/cleanup.h"
#include "source/common/common/thread.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/utility.h"

namespace Envoy {
namespace Stats {

/**
 * Lazy-initialization wrapper for StatsStructType, intended for deferred instantiation of a block
 * of stats that might not be needed in a given Envoy process.
 *
 * This class is thread-safe -- instantiations can occur on multiple concurrent threads.
 * This is used when
 * :ref:`enable_deferred_creation_stats
 * <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.deferred_stat_options>` is enabled.
 */
template <typename StatsStructType>
class DeferredStats : public DeferredCreationCompatibleInterface<StatsStructType> {
public:
  // Capture the stat names object and the scope with a ctor, that can be used to instantiate a
  // StatsStructType object later.
  // Caller should make sure scope and stat_names outlive this object.
  DeferredStats(const typename StatsStructType::StatNameType& stat_names,
                Stats::ScopeSharedPtr scope)
      : initialized_(
            // A lambda is used as we need to register the name into the symbol table.
            // Note: there is no issue to capture a reference of the scope here as this lambda is
            // only used to initialize the 'initialized_' Gauge.
            [&scope]() -> Gauge& {
              Stats::StatNamePool pool(scope->symbolTable());
              return Stats::Utility::gaugeFromElements(
                  *scope, {pool.add(StatsStructType::typeName()), pool.add("initialized")},
                  Stats::Gauge::ImportMode::HiddenAccumulate);
            }()),
        ctor_([this, &stat_names, stats_scope = std::move(scope)]() -> StatsStructType* {
          initialized_.inc();
          // Reset ctor_ to save some RAM.
          Cleanup reset_ctor([this] { ctor_ = nullptr; });
          return new StatsStructType(stat_names, *stats_scope);
        }) {
    if (initialized_.value() > 0) {
      getOrCreateHelper();
    }
  }
  ~DeferredStats() override {
    if (ctor_ == nullptr) {
      initialized_.dec();
    }
  }
  inline StatsStructType& getOrCreate() override { return getOrCreateHelper(); }
  bool isPresent() const override { return !internal_stats_.isNull(); }

private:
  // We can't call getOrCreate directly from constructor, otherwise the compiler complains about
  // bypassing virtual dispatch even though it's fine.
  inline StatsStructType& getOrCreateHelper() { return *internal_stats_.get(ctor_); }

  // In order to preserve stat value continuity across a config reload, we need to automatically
  // re-instantiate lazy stats when they are constructed, if there is already a live instantiation
  // to the same stats. Consider the following alternate scenarios:

  // Scenario 1: a cluster is instantiated but receives no requests, so its traffic-related stats
  // are never instantiated. When this cluster gets reloaded on a config update, a new lazy-init
  // block is created, but the stats should again not be instantiated.

  // Scenario 2: a cluster is instantiated and receives traffic, so its traffic-related stats are
  // instantiated. We must ensure that a new instance for the same cluster gets its lazy-stats
  // instantiated before the previous cluster of the same name is destructed.

  // To do that we keep an "initialized" gauge in the cluster's scope, which will be associated by
  // name to the previous generation's cluster's lazy-init block. We use the value in this shared
  // gauge to determine whether to instantiate the lazy block on construction.
  Gauge& initialized_;
  // TODO(#26957): Clean up this ctor_ by moving its ownership to AtomicPtr, and drop
  // the setter lambda when the nested object is created.
  std::function<StatsStructType*()> ctor_;
  Thread::AtomicPtr<StatsStructType, Thread::AtomicPtrAllocMode::DeleteOnDestruct> internal_stats_;
};

// Non-deferred wrapper over StatsStructType. This is used when
// :ref:`enable_deferred_creation_stats
// <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.deferred_stat_options>` is not enabled.
template <typename StatsStructType>
class DirectStats : public DeferredCreationCompatibleInterface<StatsStructType> {
public:
  DirectStats(const typename StatsStructType::StatNameType& stat_names, Stats::Scope& scope)
      : stats_(stat_names, scope) {}
  inline StatsStructType& getOrCreate() override { return stats_; }
  bool isPresent() const override { return true; }

private:
  StatsStructType stats_;
};

// Template that lazily initializes a StatsStruct.
// The bootstrap config :ref:`enable_deferred_creation_stats
// <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.deferred_stat_options>` decides if
// stats lazy initialization is enabled or not.
template <typename StatsStructType>
DeferredCreationCompatibleStats<StatsStructType>
createDeferredCompatibleStats(Stats::ScopeSharedPtr scope,
                              const typename StatsStructType::StatNameType& stat_names,
                              bool defer_creation) {
  if (defer_creation) {
    return DeferredCreationCompatibleStats<StatsStructType>(
        std::make_unique<DeferredStats<StatsStructType>>(stat_names, scope));
  } else {
    return DeferredCreationCompatibleStats<StatsStructType>(
        std::make_unique<DirectStats<StatsStructType>>(stat_names, *scope));
  }
}

} // namespace Stats
} // namespace Envoy
