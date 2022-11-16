#pragma once

#include "source/common/common/thread.h"

namespace Envoy {
namespace Stats {

/**
 * LazyInit is a wrapper with creator of the actual "$StatsStruct"
 * structure.
 * It instantiate a $StatsStruct struct when any data member is referenced.
 * See https://github.com/envoyproxy/envoy/issues/23575 for more details.
 */
template <typename StatsStructType> struct LazyInit {
  LazyInit(Stats::Scope& scope, const typename StatsStructType::StatNameType& stat_names)
      : ctor_([&scope, &stat_names]() -> StatsStructType* {
          return new StatsStructType(stat_names, scope);
        }) {}

  StatsStructType* operator->() { return internal_stats_.get(ctor_); }
  StatsStructType& operator*() { return *internal_stats_.get(ctor_); }

  std::function<StatsStructType*()> ctor_;
  Thread::AtomicPtr<StatsStructType, Thread::AtomicPtrAllocMode::DeleteOnDestruct>
      internal_stats_{};
};

} // namespace Stats
} // namespace Envoy
