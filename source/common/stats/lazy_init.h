#pragma once

#include "source/common/common/thread.h"

namespace Envoy {
namespace Stats {

template <typename StatsStructType> class DirectStats;
template <typename StatsStructType> class LazyInit;

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
  // Helper function to get-or-create and return the StatsStructType object.
  virtual StatsStructType* instantiate() = 0;

  virtual ~LazyCompatibleInterface() = default;
};

/**
 * Lazy-initialization wrapper for StatsStructType, intended for deferred instantiation of a block
 * of stats that might not be needed in a given Envoy process.
 *
 * This class is thread-safe -- instantiations can occur on multiple concurrent threads.
 * This is used when
 * :ref:`enable_lazyinit_stats
 * <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.enable_lazyinit_stats>` is enabled.
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
      instantiate();
      ctor_ = nullptr;
    }
  }
  ~LazyInit() {
    if (inited_.value() > 0) {
      inited_.dec();
    }
  }

private:
  inline StatsStructType* instantiate() override { return internal_stats_.get(ctor_); }

  // In order to preserve stat value continuity across a config reload, we need to automatically
  // re-instantiate lazy stats when they are constructed, if there is already a live instantiation
  // to the same stats. Consider the following alternate scenarios:

  // Scenario 1: a cluster is instantiated but receives no requests, so its traffic-related stats
  // are never instantiated. When this cluster gets reloaded on a config update, a new lazy-init
  // block is created, but the stats should again not be instaniated.

  // Scenario 2: a cluster is instantiated and receives traffic, so its traffic-related stats are
  // instantiated. We must ensure that a new instance for the same cluster gets its lazy-stats
  // instantiated before the previous cluster of the same name is destructed.

  // To do that we keep an "inited" stat in the cluster's scope, which will be associated by name to
  // the previous generation's cluster's lazy-init block. We use the value in this shared gauge to
  // determine whether to instantiate the lazy block on construction.
  Gauge& inited_;
  // TODO(stevenzzzz, jmarantz): Clean up this ctor_ by moving its ownership to AtomicPtr, and drop
  // the setter lambda when the nested object is created.
  std::function<StatsStructType*()> ctor_;
  Thread::AtomicPtr<StatsStructType, Thread::AtomicPtrAllocMode::DeleteOnDestruct>
      internal_stats_{};
};

// Non-LazyInit wrapper over StatsStructType. This is used when
// :ref:`enable_lazyinit_stats
// <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.enable_lazyinit_stats>` is not enabled.
template <typename StatsStructType>
class DirectStats : public LazyCompatibleInterface<StatsStructType> {
public:
  DirectStats(const typename StatsStructType::StatNameType& stat_names, Stats::Scope& scope)
      : stats_(stat_names, scope) {}

private:
  inline StatsStructType* instantiate() override { return &stats_; }
  StatsStructType stats_;
};

// A helper class to remove the double indirections on "operator ->/&" for a lazy compatible stats
// struct.
template <typename StatsStructType> class LazyCompatibleStats {
public:
  static LazyCompatibleStats create(Stats::ScopeSharedPtr scope,
                                    const typename StatsStructType::StatNameType& stat_names,
                                    bool lazyinit) {
    if (lazyinit) {
      return {std::make_unique<LazyInit<StatsStructType>>(stat_names, scope)};
    } else {
      return {std::make_unique<DirectStats<StatsStructType>>(stat_names, *scope)};
    }
  }

  // Allows move construct and assign.
  LazyCompatibleStats& operator=(LazyCompatibleStats&&) = default;
  LazyCompatibleStats(LazyCompatibleStats&&) = default;

  inline StatsStructType* operator->() { return data_->instantiate(); };
  inline StatsStructType& operator*() { return *data_->instantiate(); };

private:
  LazyCompatibleStats(std::unique_ptr<LazyCompatibleInterface<StatsStructType>> d)
      : data_(std::move(d)) {}

  std::unique_ptr<LazyCompatibleInterface<StatsStructType>> data_;
};

} // namespace Stats
} // namespace Envoy
