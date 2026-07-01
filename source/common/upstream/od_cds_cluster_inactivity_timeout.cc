#include "source/common/upstream/od_cds_cluster_inactivity_timeout.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace Envoy {
namespace Upstream {

OdCdsClusterInactivityTimeout::OdCdsClusterInactivityTimeout(
    Event::Dispatcher& main_thread_dispatcher, TimeSource& time_source,
    OdCdsClusterInactivityTimeout::PollClusterActivityStatsCb sample_activity,
    OdCdsClusterInactivityTimeout::ReclaimClusterCb reclaim_cluster)
    : time_source_(time_source), sample_activity_(std::move(sample_activity)),
      reclaim_cluster_(std::move(reclaim_cluster)),
      sweep_timer_(main_thread_dispatcher.createTimer([this]() { sweep(); })) {}

void OdCdsClusterInactivityTimeout::onClusterDiscovered(
    absl::string_view cluster_name, std::chrono::milliseconds cluster_inactivity_timeout) {
  if (cluster_inactivity_timeout <= std::chrono::milliseconds::zero()) {
    // Inactivity timeout disabled for this cluster: drop any stale tracking from an earlier
    // discovery that carried a timeout.
    tracked_.erase(cluster_name);
    min_tracked_inactivity_timeout_ = std::chrono::milliseconds::min();
    return;
  }

  ClusterState& state = tracked_[cluster_name];
  if (state.inactivity_timeout != cluster_inactivity_timeout) {
    state.inactivity_timeout = cluster_inactivity_timeout;
    min_tracked_inactivity_timeout_ = std::chrono::milliseconds::min();
  }
  const std::optional<ClusterActivityStats> sampled = sample_activity_(cluster_name);
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
  min_tracked_inactivity_timeout_ = std::chrono::milliseconds::min();
}

void OdCdsClusterInactivityTimeout::armSweepTimer() {
  if (tracked_.empty() || sweep_timer_->enabled()) {
    return;
  }

  if (min_tracked_inactivity_timeout_ < std::chrono::milliseconds::zero()) {
    // Sweep at the shortest tracked timeout so every cluster is reclaimed within [timeout,
    // 2*timeout] of its last stream. When all clusters share a timeout (the common case) this
    // is exactly that timeout.
    std::chrono::milliseconds cadence = std::chrono::milliseconds::max();
    for (const auto& [cluster_name, state] : tracked_) {
      cadence = std::min(cadence, state.inactivity_timeout);
    }
    min_tracked_inactivity_timeout_ = cadence;
  }
  sweep_timer_->enableTimer(min_tracked_inactivity_timeout_);
}

void OdCdsClusterInactivityTimeout::sweep() {
  const MonotonicTime now = time_source_.monotonicTime();

  for (auto it = tracked_.begin(); it != tracked_.end();) {
    auto& [cluster_name, state] = *it;
    const std::optional<ClusterActivityStats> sampled = sample_activity_(cluster_name);
    if (!sampled) {
      tracked_.erase(it++);
      min_tracked_inactivity_timeout_ = std::chrono::milliseconds::min();
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
    if (since_last_active >= state.inactivity_timeout) {
      ENVOY_LOG(debug, "odcds: reclaiming on-demand cluster '{}' idle for {} >= {} ms",
                cluster_name, since_last_active, state.inactivity_timeout.count());
      reclaim_cluster_(cluster_name);
      tracked_.erase(it++);
      min_tracked_inactivity_timeout_ = std::chrono::milliseconds::min();
    } else {
      ++it;
    }
  }

  armSweepTimer();
}

} // namespace Upstream
} // namespace Envoy
