#include "source/common/upstream/od_cds_cluster_inactivity_timeout.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/container/inlined_vector.h"

namespace Envoy {
namespace Upstream {
namespace {

struct ClusterStats {
  uint64_t active_rq{0};
  uint64_t total_rq{0};
};

// Reads the cluster's live active-stream and cumulative-request counts on the main thread.
// Returns nullopt if the cluster is no longer an active cluster in the manager.
std::optional<ClusterStats> sampleActivity(const ClusterManager& cm,
                                           absl::string_view cluster_name) {
  const OptRef<const Cluster> cluster = cm.getActiveCluster(std::string(cluster_name));
  if (!cluster.has_value()) {
    return std::nullopt;
  }
  const ClusterInfoConstSharedPtr info = cluster->info();
  return ClusterStats{
      .active_rq = info->trafficStats()->upstream_rq_active_.value(),
      .total_rq = info->trafficStats()->upstream_rq_total_.value(),
  };
}

} // namespace

OdCdsClusterInactivityTimeout::OdCdsClusterInactivityTimeout(
    ClusterManager& cm, Event::Dispatcher& main_thread_dispatcher, TimeSource& time_source, std::chrono::milliseconds cluster_inactivity_timeout)
    : cm_(cm), time_source_(time_source),
      sweep_timer_(main_thread_dispatcher.createTimer([this]() { sweep(); })), cluster_inactivity_timeout_(cluster_inactivity_timeout)  {}

void OdCdsClusterInactivityTimeout::onClusterDiscovered(absl::string_view cluster_name) {
  if (cluster_inactivity_timeout_ <= std::chrono::milliseconds::zero()) {
    // Inactivity timeout disabled.
    return;
  }

  ClusterState& state = tracked_[cluster_name];
  const std::optional<ClusterStats> sampled = sampleActivity(cm_, cluster_name);
  // Best-effort baseline for the request counter; the cluster may not be active yet, in
  // which case the next sweep establishes the baseline instead.
  state.last_rq_total = sampled ? sampled->total_rq : 0;
  // Treat (re-)discovery as activity so a just-discovered cluster survives at least one
  // inactivity timeout before it can be reclaimed.
  state.last_active = time_source_.monotonicTime();
  armSweepTimer();
}

void OdCdsClusterInactivityTimeout::onClusterRemoved(absl::string_view cluster_name) {
  tracked_.erase(cluster_name);
}

void OdCdsClusterInactivityTimeout::armSweepTimer() {
  if (tracked_.empty() || sweep_timer_->enabled()) {
    return;
  }

  sweep_timer_->enableTimer(cluster_inactivity_timeout_);
}

void OdCdsClusterInactivityTimeout::sweep() {
  const MonotonicTime now = time_source_.monotonicTime();

  for (auto it = tracked_.begin(); it != tracked_.end();) {
    auto& [cluster_name, state] = *it;
    const std::optional<ClusterStats> sampled = sampleActivity(cm_, cluster_name);
    if (!sampled) {
      tracked_.erase(it++);
      continue;
    }

    const bool active_since_last_sweep =
        sampled->active_rq > 0 || sampled->total_rq != state.last_rq_total;
    state.last_rq_total = sampled->total_rq;
    if (active_since_last_sweep) {
      state.last_active = now;
      ++it;
      continue;
    }

    const auto since_last_active = now - state.last_active;
    if (since_last_active >= cluster_inactivity_timeout_) {
      ENVOY_LOG(debug, "odcds: reclaiming on-demand cluster '{}' idle for {} >= {} ms",
                cluster_name, since_last_active, cluster_inactivity_timeout_.count());
      cm_.removeCluster(cluster_name);
      tracked_.erase(it++);
    } else {
      ++it;
    }
  }

  armSweepTimer();
}

} // namespace Upstream
} // namespace Envoy
