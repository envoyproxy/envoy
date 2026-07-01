#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/logger.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Upstream {

// Reclaims clusters discovered on-demand once they have gone idle for their
// configured inactivity timeout. The clock counts from when the last stream
// through the cluster ended: a cluster is removed only after it has had no
// active upstream requests and served no new requests for the whole timeout
// duration.
//
// All methods must be called on the main thread, which is where ODCDS runs and
// where ClusterManager cluster add/remove is safe.
class OdCdsClusterInactivityTimeout : Logger::Loggable<Logger::Id::upstream> {
public:
  OdCdsClusterInactivityTimeout(ClusterManager& cm, Event::Dispatcher& main_thread_dispatcher,
                                TimeSource& time_source,
                                std::chrono::milliseconds cluster_inactivity_timeout);

  // Begins, or refreshes, idle tracking for a cluster that was just
  // (re-)delivered by ODCDS.
  void onClusterDiscovered(absl::string_view cluster_name);

  // Drops a cluster from tracking because the discovery server removed it.
  void onClusterRemoved(absl::string_view cluster_name);

private:
  struct ClusterState {
    uint64_t last_rq_total{0};
    // Time of the most recent sweep at which the cluster was seen active. Removal fires
    // once (now - last_active) reaches this cluster's inactivity timeout.
    MonotonicTime last_active{};
  };

  // Periodic idle check over all tracked clusters.
  void sweep();

  // Arms the sweep timer (when clusters are tracked and it is not already running) at a
  // cadence equal to the shortest tracked timeout, so no cluster waits more than ~2x its
  // own inactivity timeout to be reclaimed.
  void armSweepTimer();

  ClusterManager& cm_;
  TimeSource& time_source_;
  const Event::TimerPtr sweep_timer_;
  const std::chrono::milliseconds cluster_inactivity_timeout_;
  absl::flat_hash_map<std::string, ClusterState> tracked_;
};

using OdCdsClusterInactivityTimeoutPtr = std::unique_ptr<OdCdsClusterInactivityTimeout>;

} // namespace Upstream
} // namespace Envoy
