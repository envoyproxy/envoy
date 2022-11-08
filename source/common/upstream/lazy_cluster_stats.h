#pragma once

#include "envoy/upstream/upstream.h"
#include "source/common/common/thread.h"

namespace Envoy {
namespace Upstream {

struct ClusterStats {
  InstantiatedClusterStats* operator->() { return instantiate(); }
  InstantiatedClusterStats& operator*() { return *instantiate(); }

  InstantiatedClusterStats* instantiate() {
    return cluster_stats_.get([this]() -> InstantiatedClusterStats* {
        return new InstantiatedClusterStats{stat_names_, scope_};
    });
  }

  Thread::AtomicPtr<InstantiatedClusterStats,
      Thread::AtomicPtrAllocMode::DeleteOnDestruct> cluster_stats_;
  Stats::Scope& scope_;
  const ClusterStatNames& stat_names_;
};

} // namespace Upstream
} // namespace Envoy
