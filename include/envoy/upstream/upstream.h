#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/api/v2/base.pb.h"
#include "envoy/common/callback.h"
#include "envoy/common/optional.h"
#include "envoy/http/codec.h"
#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"
#include "envoy/ssl/context.h"
#include "envoy/upstream/health_check_host_monitor.h"
#include "envoy/upstream/load_balancer_type.h"
#include "envoy/upstream/outlier_detection.h"
#include "envoy/upstream/resource_manager.h"

namespace Envoy {
namespace Upstream {

/**
 * An upstream host.
 */
class Host : virtual public HostDescription {
public:
  struct CreateConnectionData {
    Network::ClientConnectionPtr connection_;
    HostDescriptionConstSharedPtr host_description_;
  };

  enum class HealthFlag {
    // The host is currently failing active health checks.
    FAILED_ACTIVE_HC = 0x1,
    // The host is currently considered an outlier and has been ejected.
    FAILED_OUTLIER_CHECK = 0x02
  };

  /**
   * @return host specific counters.
   */
  virtual std::list<Stats::CounterSharedPtr> counters() const PURE;

  /**
   * Create a connection for this host.
   * @param dispatcher supplies the owning dispatcher.
   * @return the connection data which includes the raw network connection as well as the *real*
   *         host that backs it. The reason why a 2nd host is returned is that some hosts are
   *         logical and wrap multiple real network destinations. In this case, a different host
   *         will be returned along with the connection vs. the host the method was called on.
   *         If it matters, callers should not assume that the returned host will be the same.
   */
  virtual CreateConnectionData createConnection(Event::Dispatcher& dispatcher) const PURE;

  /**
   * @return host specific gauges.
   */
  virtual std::list<Stats::GaugeSharedPtr> gauges() const PURE;

  /**
   * Atomically clear a health flag for a host. Flags are specified in HealthFlags.
   */
  virtual void healthFlagClear(HealthFlag flag) PURE;

  /**
   * Atomically get whether a health flag is set for a host. Flags are specified in HealthFlags.
   */
  virtual bool healthFlagGet(HealthFlag flag) const PURE;

  /**
   * Atomically set a health flag for a host. Flags are specified in HealthFlags.
   */
  virtual void healthFlagSet(HealthFlag flag) PURE;

  /**
   * @return whether in aggregate a host is healthy and routable. Multiple health flags and other
   *         information may be considered.
   */
  virtual bool healthy() const PURE;

  /**
   * Set the host's health checker monitor. Monitors are assumed to be thread safe, however
   * a new monitor must be installed before the host is used across threads. Thus,
   * this routine should only be called on the main thread before the host is used across threads.
   */
  virtual void setHealthChecker(HealthCheckHostMonitorPtr&& health_checker) PURE;

  /**
   * Set the host's outlier detector monitor. Outlier detector monitors are assumed to be thread
   * safe, however a new outlier detector monitor must be installed before the host is used across
   * threads. Thus, this routine should only be called on the main thread before the host is used
   * across threads.
   */
  virtual void setOutlierDetector(Outlier::DetectorHostMonitorPtr&& outlier_detector) PURE;

  /**
   * @return the current load balancing weight of the host, in the range 1-100.
   */
  virtual uint32_t weight() const PURE;

  /**
   * Set the current load balancing weight of the host, in the range 1-100.
   */
  virtual void weight(uint32_t new_weight) PURE;

  /**
   * @return the current boolean value of host being in use.
   */
  virtual bool used() const PURE;

  /**
   * @param new_used supplies the new value of host being in use to be stored.
   */
  virtual void used(bool new_used) PURE;
};

typedef std::shared_ptr<const Host> HostConstSharedPtr;

/**
 * Base host set interface. This contains all of the endpoints for a given LocalityLbEndpoints
 * priority level.
 */
class HostSet {
public:
  typedef std::shared_ptr<const std::vector<HostSharedPtr>> HostVectorConstSharedPtr;
  typedef std::shared_ptr<const std::vector<std::vector<HostSharedPtr>>> HostListsConstSharedPtr;

  virtual ~HostSet() {}

  /**
   * @return all hosts that make up the set at the current time.
   */
  virtual const std::vector<HostSharedPtr>& hosts() const PURE;

  /**
   * @return all healthy hosts contained in the set at the current time. NOTE: This set is
   *         eventually consistent. There is a time window where a host in this set may become
   *         unhealthy and calling healthy() on it will return false. Code should be written to
   *         deal with this case if it matters.
   */
  virtual const std::vector<HostSharedPtr>& healthyHosts() const PURE;

  /**
   * @return hosts per locality, index 0 is dedicated to local locality hosts.
   * If there are no hosts in local locality for upstream cluster hostsPerLocality() will @return
   * empty vector.
   *
   * Note, that we sort localities in lexicographic order starting from index 1.
   */
  virtual const std::vector<std::vector<HostSharedPtr>>& hostsPerLocality() const PURE;

  /**
   * @return same as hostsPerLocality but only contains healthy hosts.
   */
  virtual const std::vector<std::vector<HostSharedPtr>>& healthyHostsPerLocality() const PURE;

  /**
   * Updates the hosts in a given host set.
   *
   * @param hosts supplies the (usually new) list of hosts in the host set.
   * @param healthy hosts supplies the subset of hosts which are healthy.
   * @param hosts_per_locality supplies the hosts subdivided by locality.
   * @param hosts_per_locality supplies the healthy hosts subdivided by locality.
   * @param hosts_added supplies the hosts added since the last update.
   * @param hosts_removed supplies the hosts removed since the last update.
   */
  virtual void updateHosts(HostVectorConstSharedPtr hosts, HostVectorConstSharedPtr healthy_hosts,
                           HostListsConstSharedPtr hosts_per_locality,
                           HostListsConstSharedPtr healthy_hosts_per_locality,
                           const std::vector<HostSharedPtr>& hosts_added,
                           const std::vector<HostSharedPtr>& hosts_removed) PURE;

  /**
   * @return uint32_t the priority of this host set.
   */
  virtual uint32_t priority() const PURE;
};

typedef std::unique_ptr<HostSet> HostSetPtr;

/**
 * This class contains all of the HostSets for a given cluster grouped by priority, for
 * ease of load balancing.
 */
class PrioritySet {
public:
  typedef std::function<void(uint32_t priority, const std::vector<HostSharedPtr>& hosts_added,
                             const std::vector<HostSharedPtr>& hosts_removed)>
      MemberUpdateCb;

  virtual ~PrioritySet() {}

  /**
   * Install a callback that will be invoked when any of the HostSets in the PrioritySet changes.
   * This includes when a new HostSet is created.
   *
   * @param callback supplies the callback to invoke.
   * @return Common::CallbackHandle* a handle which can be used to unregister the callback.
   */
  virtual Common::CallbackHandle* addMemberUpdateCb(MemberUpdateCb callback) const PURE;

  /**
   * Returns the host sets for this priority set, ordered by priority.
   * The first element in the vector is the host set for priority 0, and so on.
   *
   * @return std::vector<HostSetPtr>& the host sets for this priority set.
   */
  virtual std::vector<HostSetPtr>& hostSetsPerPriority() PURE;

  /**
   * @return const std::vector<HostSetPtr>& the host sets, ordered by priority.
   */
  virtual const std::vector<HostSetPtr>& hostSetsPerPriority() const PURE;
};

/**
 * All cluster stats. @see stats_macros.h
 */
// clang-format off
#define ALL_CLUSTER_STATS(COUNTER, GAUGE, HISTOGRAM)                                               \
  COUNTER  (lb_healthy_panic)                                                                      \
  COUNTER  (lb_local_cluster_not_ok)                                                               \
  COUNTER  (lb_recalculate_zone_structures)                                                        \
  COUNTER  (lb_zone_cluster_too_small)                                                             \
  COUNTER  (lb_zone_no_capacity_left)                                                              \
  COUNTER  (lb_zone_number_differs)                                                                \
  COUNTER  (lb_zone_routing_all_directly)                                                          \
  COUNTER  (lb_zone_routing_sampled)                                                               \
  COUNTER  (lb_zone_routing_cross_zone)                                                            \
  GAUGE    (lb_subsets_active)                                                                     \
  COUNTER  (lb_subsets_created)                                                                    \
  COUNTER  (lb_subsets_removed)                                                                    \
  COUNTER  (lb_subsets_selected)                                                                   \
  COUNTER  (lb_subsets_fallback)                                                                   \
  COUNTER  (upstream_cx_total)                                                                     \
  GAUGE    (upstream_cx_active)                                                                    \
  COUNTER  (upstream_cx_http1_total)                                                               \
  COUNTER  (upstream_cx_http2_total)                                                               \
  COUNTER  (upstream_cx_connect_fail)                                                              \
  COUNTER  (upstream_cx_connect_timeout)                                                           \
  COUNTER  (upstream_cx_connect_attempts_exceeded)                                                 \
  COUNTER  (upstream_cx_overflow)                                                                  \
  HISTOGRAM(upstream_cx_connect_ms)                                                                \
  HISTOGRAM(upstream_cx_length_ms)                                                                 \
  COUNTER  (upstream_cx_destroy)                                                                   \
  COUNTER  (upstream_cx_destroy_local)                                                             \
  COUNTER  (upstream_cx_destroy_remote)                                                            \
  COUNTER  (upstream_cx_destroy_with_active_rq)                                                    \
  COUNTER  (upstream_cx_destroy_local_with_active_rq)                                              \
  COUNTER  (upstream_cx_destroy_remote_with_active_rq)                                             \
  COUNTER  (upstream_cx_close_notify)                                                              \
  COUNTER  (upstream_cx_rx_bytes_total)                                                            \
  GAUGE    (upstream_cx_rx_bytes_buffered)                                                         \
  COUNTER  (upstream_cx_tx_bytes_total)                                                            \
  GAUGE    (upstream_cx_tx_bytes_buffered)                                                         \
  COUNTER  (upstream_cx_protocol_error)                                                            \
  COUNTER  (upstream_cx_max_requests)                                                              \
  COUNTER  (upstream_cx_none_healthy)                                                              \
  COUNTER  (upstream_rq_total)                                                                     \
  GAUGE    (upstream_rq_active)                                                                    \
  COUNTER  (upstream_rq_pending_total)                                                             \
  COUNTER  (upstream_rq_pending_overflow)                                                          \
  COUNTER  (upstream_rq_pending_failure_eject)                                                     \
  GAUGE    (upstream_rq_pending_active)                                                            \
  COUNTER  (upstream_rq_cancelled)                                                                 \
  COUNTER  (upstream_rq_maintenance_mode)                                                          \
  COUNTER  (upstream_rq_timeout)                                                                   \
  COUNTER  (upstream_rq_per_try_timeout)                                                           \
  COUNTER  (upstream_rq_rx_reset)                                                                  \
  COUNTER  (upstream_rq_tx_reset)                                                                  \
  COUNTER  (upstream_rq_retry)                                                                     \
  COUNTER  (upstream_rq_retry_success)                                                             \
  COUNTER  (upstream_rq_retry_overflow)                                                            \
  COUNTER  (upstream_flow_control_paused_reading_total)                                            \
  COUNTER  (upstream_flow_control_resumed_reading_total)                                           \
  COUNTER  (upstream_flow_control_backed_up_total)                                                 \
  COUNTER  (upstream_flow_control_drained_total)                                                   \
  COUNTER  (bind_errors)                                                                           \
  GAUGE    (max_host_weight)                                                                       \
  COUNTER  (membership_change)                                                                     \
  GAUGE    (membership_healthy)                                                                    \
  GAUGE    (membership_total)                                                                      \
  COUNTER  (retry_or_shadow_abandoned)                                                             \
  COUNTER  (update_attempt)                                                                        \
  COUNTER  (update_success)                                                                        \
  COUNTER  (update_failure)                                                                        \
  COUNTER  (update_empty)                                                                          \
  GAUGE    (version)
// clang-format on

/**
 * All cluster load report stats. These are only use for EDS load reporting and not sent to the
 * stats sink. See envoy.api.v2.endpoint.ClusterStats for the definition of upstream_rq_dropped.
 * These are latched by LoadStatsReporter, independent of the normal stats sink flushing.
 */
// clang-format off
#define ALL_CLUSTER_LOAD_REPORT_STATS(COUNTER)                                                     \
  COUNTER (upstream_rq_dropped)
// clang-format on

/**
 * Struct definition for all cluster stats. @see stats_macros.h
 */
struct ClusterStats {
  ALL_CLUSTER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

/**
 * Struct definition for all cluster load report stats. @see stats_macros.h
 */
struct ClusterLoadReportStats {
  ALL_CLUSTER_LOAD_REPORT_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Information about a given upstream cluster.
 */
class ClusterInfo {
public:
  struct Features {
    // Whether the upstream supports HTTP2. This is used when creating connection pools.
    static const uint64_t HTTP2 = 0x1;
    // Use the downstream protocol (HTTP1.1, HTTP2) for upstream connections as well, if available.
    // This is used when creating connection pools.
    static const uint64_t USE_DOWNSTREAM_PROTOCOL = 0x2;
  };

  virtual ~ClusterInfo() {}

  /**
   * @return bool whether the cluster was added via API (if false the cluster was present in the
   *         initial configuration and cannot be removed or updated).
   */
  virtual bool addedViaApi() const PURE;

  /**
   * @return the connect timeout for upstream hosts that belong to this cluster.
   */
  virtual std::chrono::milliseconds connectTimeout() const PURE;

  /**
   * @return soft limit on size of the cluster's connections read and write buffers.
   */
  virtual uint32_t perConnectionBufferLimitBytes() const PURE;

  /**
   * @return uint64_t features supported by the cluster. @see Features.
   */
  virtual uint64_t features() const PURE;

  /**
   * @return const Http::Http2Settings& for HTTP/2 connections created on behalf of this cluster.
   *         @see Http::Http2Settings.
   */
  virtual const Http::Http2Settings& http2Settings() const PURE;

  /**
   * @return the type of load balancing that the cluster should use.
   */
  virtual LoadBalancerType lbType() const PURE;

  /**
   * @return the service discovery type to use for resolving the cluster.
   */
  virtual envoy::api::v2::Cluster::DiscoveryType type() const PURE;

  /**
   * @return configuration for ring hash load balancing, only used if type is set to ring_hash_lb.
   */
  virtual const Optional<envoy::api::v2::Cluster::RingHashLbConfig>& lbRingHashConfig() const PURE;

  /**
   * @return Whether the cluster is currently in maintenance mode and should not be routed to.
   *         Different filters may handle this situation in different ways. The implementation
   *         of this routine is typically based on randomness and may not return the same answer
   *         on each call.
   */
  virtual bool maintenanceMode() const PURE;

  /**
   * @return uint64_t the maximum number of outbound requests that a connection pool will make on
   *         each upstream connection. This can be used to increase spread if the backends cannot
   *         tolerate imbalance. 0 indicates no maximum.
   */
  virtual uint64_t maxRequestsPerConnection() const PURE;

  /**
   * @return the human readable name of the cluster.
   */
  virtual const std::string& name() const PURE;

  /**
   * @return ResourceManager& the resource manager to use by proxy agents for this cluster (at
   *         a particular priority).
   */
  virtual ResourceManager& resourceManager(ResourcePriority priority) const PURE;

  /**
   * @return Network::TransportSocketFactory& the factory of transport socket to use when
   *         communicating with the cluster.
   */
  virtual Network::TransportSocketFactory& transportSocketFactory() const PURE;

  /**
   * @return ClusterStats& strongly named stats for this cluster.
   */
  virtual ClusterStats& stats() const PURE;

  /**
   * @return the stats scope that contains all cluster stats. This can be used to produce dynamic
   *         stats that will be freed when the cluster is removed.
   */
  virtual Stats::Scope& statsScope() const PURE;

  /**
   * @return ClusterLoadReportStats& strongly named load report stats for this cluster.
   */
  virtual ClusterLoadReportStats& loadReportStats() const PURE;

  /**
   * Returns an optional source address for upstream connections to bind to.
   *
   * @return a source address to bind to or nullptr if no bind need occur.
   */
  virtual const Network::Address::InstanceConstSharedPtr& sourceAddress() const PURE;

  /**
   * @return the configuration for load balancer subsets.
   */
  virtual const LoadBalancerSubsetInfo& lbSubsetInfo() const PURE;

  /**
   * @return const envoy::api::v2::Metadata& the configuration metadata for this cluster.
   */
  virtual const envoy::api::v2::Metadata& metadata() const PURE;
};

typedef std::shared_ptr<const ClusterInfo> ClusterInfoConstSharedPtr;

class HealthChecker;

/**
 * An upstream cluster (group of hosts). This class is the "primary" singleton cluster used amongst
 * all forwarding threads/workers. Individual HostSets are used on the workers themselves.
 */
class Cluster {
public:
  virtual ~Cluster() {}

  enum class InitializePhase { Primary, Secondary };

  /**
   * @return a pointer to the cluster's health checker. If a health checker has not been installed,
   *         returns nullptr.
   */
  virtual HealthChecker* healthChecker() PURE;

  /**
   * @return the information about this upstream cluster.
   */
  virtual ClusterInfoConstSharedPtr info() const PURE;

  /**
   * @return a pointer to the cluster's outlier detector. If an outlier detector has not been
   *         installed, returns nullptr.
   */
  virtual Outlier::Detector* outlierDetector() PURE;
  virtual const Outlier::Detector* outlierDetector() const PURE;

  /**
   * Initialize the cluster. This will be called either immediately at creation or after all primary
   * clusters have been initialized (determined via initializePhase()).
   * @param callback supplies a callback that will be invoked after the cluster has undergone first
   *        time initialization. E.g., for a dynamic DNS cluster the initialize callback will be
   *        called when initial DNS resolution is complete.
   */
  virtual void initialize(std::function<void()> callback) PURE;

  /**
   * @return the phase in which the cluster is initialized at boot. This mechanism is used such that
   *         clusters that depend on other clusters can correctly initialize. (E.g., an SDS cluster
   *         that depends on resolution of the SDS server itself).
   */
  virtual InitializePhase initializePhase() const PURE;

  /**
   * @return the PrioritySet for the cluster.
   */
  virtual PrioritySet& prioritySet() PURE;

  /**
   * @return the const PrioritySet for the cluster.
   */
  virtual const PrioritySet& prioritySet() const PURE;
};

typedef std::shared_ptr<Cluster> ClusterSharedPtr;

} // namespace Upstream
} // namespace Envoy
