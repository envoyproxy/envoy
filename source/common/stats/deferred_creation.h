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
 * <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.enable_deferred_creation_stats>` is enabled.
 */
template <typename StatsStructType>
class DeferredCreation : public DeferredCreationCompatibleInterface<StatsStructType> {
public:
  // Capture the stat names object and the scope with a ctor, that can be used to instantiate a
  // StatsStructType object later.
  // Caller should make sure scope and stat_names outlive this object.
  DeferredCreation(const typename StatsStructType::StatNameType& stat_names,
                   Stats::ScopeSharedPtr scope)
      : initialized_([&scope]() -> Gauge& {
          Stats::StatNamePool pool(scope->symbolTable());
          return Stats::Utility::gaugeFromElements(
              *scope, {pool.add(StatsStructType::typeName()), pool.add("initialized")},
              Stats::Gauge::ImportMode::HiddenAccumulate);
        }()),
        ctor_([this, stats_scope = std::move(scope)]() -> StatsStructType* {
          initialized_.inc();
          // Reset ctor_ to save some RAM.
          Cleanup reset_ctor([this] { ctor_ = nullptr; });
          return new StatsStructType(stat_names, *stats_scope);
        }) {
    if (initialized_.value() > 0) {
      instantiate();
    }
  }
  ~DeferredCreation() {
    if (ctor_ == nullptr) {
      initialized_.dec();
    }
  }

private:
  inline StatsStructType& instantiate() override { return *internal_stats_.get(ctor_); }

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
  // TODO(#26106): See #14610. The initialized_ gauge could be disabled in a
  // corner case where a user disables stats with suffix "initialized". In which case, the
  // initialized_ will be a NullGauge, which breaks the above scenario 2.
  Gauge& initialized_;
  // TODO(#26957): Clean up this ctor_ by moving its ownership to AtomicPtr, and drop
  // the setter lambda when the nested object is created.
  std::function<StatsStructType*()> ctor_;
  Thread::AtomicPtr<StatsStructType, Thread::AtomicPtrAllocMode::DeleteOnDestruct> internal_stats_;
};

// Non-DeferredCreation wrapper over StatsStructType. This is used when
// :ref:`enable_deferred_creation_stats
// <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.enable_deferred_creation_stats>` is not
// enabled.
template <typename StatsStructType>
class DirectStats : public DeferredCreationCompatibleInterface<StatsStructType> {
public:
  DirectStats(const typename StatsStructType::StatNameType& stat_names, Stats::Scope& scope)
      : stats_(stat_names, scope) {}

private:
  inline StatsStructType& instantiate() override { return stats_; }
  StatsStructType stats_;
};

template <typename StatsStructType>
DeferredCreationCompatibleStats<StatsStructType>
createDeferredCompatibleStats(Stats::ScopeSharedPtr scope,
                              const typename StatsStructType::StatNameType& stat_names,
                              bool deferred_creation) {
  if (deferred_creation) {
    return {std::make_unique<DeferredCreation<StatsStructType>>(stat_names, scope)};
  } else {
    return {std::make_unique<DirectStats<StatsStructType>>(stat_names, *scope)};
  }
}

} // namespace Stats
} // namespace Envoy
