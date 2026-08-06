#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"

#include "source/common/common/logger.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Upstream {

// Reclaims clusters discovered on-demand once they have gone idle for their
// configured idle timeout. The clock counts from when the last stream
// through the cluster ended: a cluster is removed only after it has had no
// active upstream requests and served no new requests for the whole timeout
// duration.
//
// The class does not depend on ClusterManager directly: idleness is observed
// through an injected sampler that returns the cluster's live request counts,
// and reclamation is delegated to an injected callback.
//
// All methods must be called on the main thread, where ODCDS runs and where
// cluster add/remove is safe.
class OdCdsClusterIdleTimeout : Logger::Loggable<Logger::Id::upstream> {
public:
  // Live request counts for a cluster, sampled on the main thread.
  struct ClusterActivityStats {
    uint64_t active_rq{0};
    uint64_t total_rq{0};
  };

  using PollClusterActivityStatsCb =
      std::function<std::optional<ClusterActivityStats>(absl::string_view)>;
  using ReclaimClusterCb = std::function<void(absl::string_view)>;

  // sample_activity returns a cluster's live request counts, or nullopt if it is no longer an
  // active cluster. reclaim_cluster is invoked when a cluster is reclaimed for being idle, so the
  // owner can remove the cluster and drop the ODCDS subscription's interest in the resource. Both
  // are invoked on the main thread.
  OdCdsClusterIdleTimeout(Event::Dispatcher& main_thread_dispatcher, TimeSource& time_source,
                          PollClusterActivityStatsCb sample_activity,
                          ReclaimClusterCb reclaim_cluster);

  // Begins, or refreshes, idle tracking for a cluster that was just (re-)delivered by ODCDS,
  // using the idle timeout configured on the filter that discovered it. A
  // non-positive timeout disables reclamation for the cluster (it is dropped
  // from tracking if present).
  void onClusterDiscovered(absl::string_view cluster_name,
                           std::chrono::milliseconds cluster_idle_timeout);

  // Drops a cluster from tracking because the discovery server removed it.
  void onClusterRemoved(absl::string_view cluster_name);

private:
  struct ClusterState {
    // Idle timeout for this cluster, carried from the filter that discovered it.
    std::chrono::milliseconds idle_timeout{};
    uint64_t last_rq_total{0};
    // Time of the most recent sweep at which the cluster was seen active. Removal fires
    // once (now - last_active) reaches this cluster's idle timeout.
    MonotonicTime last_active{};
  };

  // Periodic idle check over all tracked clusters.
  void sweep();

  // Arms the sweep timer (when clusters are tracked and it is not already running) at a
  // cadence equal to the shortest tracked timeout, so no cluster waits more than ~2x its
  // own idle timeout to be reclaimed.
  void armSweepTimer();

  TimeSource& time_source_;
  PollClusterActivityStatsCb sample_activity_;
  ReclaimClusterCb reclaim_cluster_;
  const Event::TimerPtr sweep_timer_;
  absl::flat_hash_map<std::string, ClusterState> tracked_;
  // Lazily-computed minimum of tracked_ values, or negative if invalidated.
  std::chrono::milliseconds min_tracked_idle_timeout_;
};

using OdCdsClusterIdleTimeoutPtr = std::unique_ptr<OdCdsClusterIdleTimeout>;

} // namespace Upstream
} // namespace Envoy
