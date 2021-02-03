#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/callback.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/http/codec.h"
#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"
#include "envoy/ssl/context.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/health_check_host_monitor.h"
#include "envoy/upstream/load_balancer_type.h"
#include "envoy/upstream/locality.h"
#include "envoy/upstream/outlier_detection.h"
#include "envoy/upstream/resource_manager.h"
#include "envoy/upstream/types.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

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

  // We use an X-macro here to make it easier to verify that all the enum values are accounted for.
  // clang-format off
#define HEALTH_FLAG_ENUM_VALUES(m)                                               \
  /* The host is currently failing active health checks. */                      \
  m(FAILED_ACTIVE_HC, 0x1)                                                       \
  /* The host is currently considered an outlier and has been ejected. */        \
  m(FAILED_OUTLIER_CHECK, 0x02)                                                  \
  /* The host is currently marked as unhealthy by EDS. */                        \
  m(FAILED_EDS_HEALTH, 0x04)                                                     \
  /* The host is currently marked as degraded through active health checking. */ \
  m(DEGRADED_ACTIVE_HC, 0x08)                                                    \
  /* The host is currently marked as degraded by EDS. */                         \
  m(DEGRADED_EDS_HEALTH, 0x10)                                                   \
  /* The host is pending removal from discovery but is stabilized due to */      \
  /* active HC. */                                                               \
  m(PENDING_DYNAMIC_REMOVAL, 0x20)                                               \
  /* The host is pending its initial active health check. */                     \
  m(PENDING_ACTIVE_HC, 0x40)                                                     \
  /* The host should be excluded from panic, spillover, etc. calculations */     \
  /* because it was explicitly taken out of rotation via protocol signal and */  \
  /* is not meant to be routed to. */                                            \
  m(EXCLUDED_VIA_IMMEDIATE_HC_FAIL, 0x80)                                        \
  /* The host failed active HC due to timeout. */                                \
  m(ACTIVE_HC_TIMEOUT, 0x100)
  // clang-format on

#define DECLARE_ENUM(name, value) name = value,

  enum class HealthFlag { HEALTH_FLAG_ENUM_VALUES(DECLARE_ENUM) };

#undef DECLARE_ENUM

  /**
   * @return host specific counters.
   */
  virtual std::vector<std::pair<absl::string_view, Stats::PrimitiveCounterReference>>
  counters() const PURE;

  /**
   * Create a connection for this host.
   * @param dispatcher supplies the owning dispatcher.
   * @param options supplies the socket options that will be set on the new connection.
   * @param transport_socket_options supplies the transport options that will be set on the new
   * connection.
   * @return the connection data which includes the raw network connection as well as the *real*
   *         host that backs it. The reason why a 2nd host is returned is that some hosts are
   *         logical and wrap multiple real network destinations. In this case, a different host
   *         will be returned along with the connection vs. the host the method was called on.
   *         If it matters, callers should not assume that the returned host will be the same.
   */
  virtual CreateConnectionData
  createConnection(Event::Dispatcher& dispatcher,
                   const Network::ConnectionSocket::OptionsSharedPtr& options,
                   Network::TransportSocketOptionsSharedPtr transport_socket_options) const PURE;

  /**
   * Create a health check connection for this host.
   * @param dispatcher supplies the owning dispatcher.
   * @param transport_socket_options supplies the transport options that will be set on the new
   * connection.
   * @return the connection data.
   */
  virtual CreateConnectionData
  createHealthCheckConnection(Event::Dispatcher& dispatcher,
                              Network::TransportSocketOptionsSharedPtr transport_socket_options,
                              const envoy::config::core::v3::Metadata* metadata) const PURE;

  /**
   * @return host specific gauges.
   */
  virtual std::vector<std::pair<absl::string_view, Stats::PrimitiveGaugeReference>>
  gauges() const PURE;

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

  enum class Health {
    /**
     * Host is unhealthy and is not able to serve traffic. A host may be marked as unhealthy either
     * through EDS or through active health checking.
     */
    Unhealthy,
    /**
     * Host is healthy, but degraded. It is able to serve traffic, but hosts that aren't degraded
     * should be preferred. A host may be marked as degraded either through EDS or through active
     * health checking.
     */
    Degraded,
    /**
     * Host is healthy and is able to serve traffic.
     */
    Healthy,
  };

  /**
   * @return the health of the host.
   */
  virtual Health health() const PURE;

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
   * @return the current load balancing weight of the host, in the range 1-128 (see
   * envoy.api.v2.endpoint.Endpoint.load_balancing_weight).
   */
  virtual uint32_t weight() const PURE;

  /**
   * Set the current load balancing weight of the host, in the range 1-128 (see
   * envoy.api.v2.endpoint.Endpoint.load_balancing_weight).
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

using HostConstSharedPtr = std::shared_ptr<const Host>;

using HostVector = std::vector<HostSharedPtr>;
using HealthyHostVector = Phantom<HostVector, Healthy>;
using DegradedHostVector = Phantom<HostVector, Degraded>;
using ExcludedHostVector = Phantom<HostVector, Excluded>;
using HostMap = absl::flat_hash_map<std::string, Upstream::HostSharedPtr>;
using HostVectorSharedPtr = std::shared_ptr<HostVector>;
using HostVectorConstSharedPtr = std::shared_ptr<const HostVector>;

using HealthyHostVectorConstSharedPtr = std::shared_ptr<const HealthyHostVector>;
using DegradedHostVectorConstSharedPtr = std::shared_ptr<const DegradedHostVector>;
using ExcludedHostVectorConstSharedPtr = std::shared_ptr<const ExcludedHostVector>;

using HostListPtr = std::unique_ptr<HostVector>;
using LocalityWeightsMap =
    absl::node_hash_map<envoy::config::core::v3::Locality, uint32_t, LocalityHash, LocalityEqualTo>;
using PriorityState = std::vector<std::pair<HostListPtr, LocalityWeightsMap>>;

/**
 * Bucket hosts by locality.
 */
class HostsPerLocality {
public:
  virtual ~HostsPerLocality() = default;

  /**
   * @return bool is local locality one of the locality buckets? If so, the
   *         local locality will be the first in the get() vector.
   */
  virtual bool hasLocalLocality() const PURE;

  /**
   * @return const std::vector<HostVector>& list of hosts organized per
   *         locality. The local locality is the first entry if
   *         hasLocalLocality() is true.
   */
  virtual const std::vector<HostVector>& get() const PURE;

  /**
   * Clone object with multiple filter predicates. Returns a vector of clones, each with host that
   * match the provided predicates.
   * @param predicates vector of predicates on Host entries.
   * @return vector of HostsPerLocalityConstSharedPtr clones of the HostsPerLocality that match
   *         hosts according to predicates.
   */
  virtual std::vector<std::shared_ptr<const HostsPerLocality>>
  filter(const std::vector<std::function<bool(const Host&)>>& predicates) const PURE;

  /**
   * Clone object.
   * @return HostsPerLocalityConstSharedPtr clone of the HostsPerLocality.
   */
  std::shared_ptr<const HostsPerLocality> clone() const {
    return filter({[](const Host&) { return true; }})[0];
  }
};

using HostsPerLocalitySharedPtr = std::shared_ptr<HostsPerLocality>;
using HostsPerLocalityConstSharedPtr = std::shared_ptr<const HostsPerLocality>;

// Weight for each locality index in HostsPerLocality.
using LocalityWeights = std::vector<uint32_t>;
using LocalityWeightsSharedPtr = std::shared_ptr<LocalityWeights>;
using LocalityWeightsConstSharedPtr = std::shared_ptr<const LocalityWeights>;

/**
 * Base host set interface. This contains all of the endpoints for a given LocalityLbEndpoints
 * priority level.
 */
// TODO(snowp): Remove the const ref accessors in favor of the shared_ptr ones.
class HostSet {
public:
  virtual ~HostSet() = default;

  /**
   * @return all hosts that make up the set at the current time.
   */
  virtual const HostVector& hosts() const PURE;

  /**
   * @return a shared ptr to the vector returned by hosts().
   */
  virtual HostVectorConstSharedPtr hostsPtr() const PURE;

  /**
   * @return all healthy hosts contained in the set at the current time. NOTE: This set is
   *         eventually consistent. There is a time window where a host in this set may become
   *         unhealthy and calling healthy() on it will return false. Code should be written to
   *         deal with this case if it matters.
   */
  virtual const HostVector& healthyHosts() const PURE;

  /**
   * @return a shared ptr to the vector returned by healthyHosts().
   */
  virtual HealthyHostVectorConstSharedPtr healthyHostsPtr() const PURE;

  /**
   * @return all degraded hosts contained in the set at the current time. NOTE: This set is
   *         eventually consistent. There is a time window where a host in this set may become
   *         undegraded and calling degraded() on it will return false. Code should be written to
   *         deal with this case if it matters.
   */
  virtual const HostVector& degradedHosts() const PURE;

  /**
   * @return a shared ptr to the vector returned by degradedHosts().
   */
  virtual DegradedHostVectorConstSharedPtr degradedHostsPtr() const PURE;

  /*
   * @return all excluded hosts contained in the set at the current time. Excluded hosts should be
   * ignored when computing load balancing weights, but may overlap with hosts in hosts().
   */
  virtual const HostVector& excludedHosts() const PURE;

  /**
   * @return a shared ptr to the vector returned by excludedHosts().
   */
  virtual ExcludedHostVectorConstSharedPtr excludedHostsPtr() const PURE;

  /**
   * @return hosts per locality.
   */
  virtual const HostsPerLocality& hostsPerLocality() const PURE;

  /**
   * @return a shared ptr to the HostsPerLocality returned by hostsPerLocality().
   */
  virtual HostsPerLocalityConstSharedPtr hostsPerLocalityPtr() const PURE;

  /**
   * @return same as hostsPerLocality but only contains healthy hosts.
   */
  virtual const HostsPerLocality& healthyHostsPerLocality() const PURE;

  /**
   * @return a shared ptr to the HostsPerLocality returned by healthyHostsPerLocality().
   */
  virtual HostsPerLocalityConstSharedPtr healthyHostsPerLocalityPtr() const PURE;

  /**
   * @return same as hostsPerLocality but only contains degraded hosts.
   */
  virtual const HostsPerLocality& degradedHostsPerLocality() const PURE;

  /**
   * @return a shared ptr to the HostsPerLocality returned by degradedHostsPerLocality().
   */
  virtual HostsPerLocalityConstSharedPtr degradedHostsPerLocalityPtr() const PURE;

  /**
   * @return same as hostsPerLocality but only contains excluded hosts.
   */
  virtual const HostsPerLocality& excludedHostsPerLocality() const PURE;

  /**
   * @return a shared ptr to the HostsPerLocality returned by excludedHostsPerLocality().
   */
  virtual HostsPerLocalityConstSharedPtr excludedHostsPerLocalityPtr() const PURE;

  /**
   * @return weights for each locality in the host set.
   */
  virtual LocalityWeightsConstSharedPtr localityWeights() const PURE;

  /**
   * @return next locality index to route to if performing locality weighted balancing
   * against healthy hosts.
   */
  virtual absl::optional<uint32_t> chooseHealthyLocality() PURE;

  /**
   * @return next locality index to route to if performing locality weighted balancing
   * against degraded hosts.
   */
  virtual absl::optional<uint32_t> chooseDegradedLocality() PURE;

  /**
   * @return uint32_t the priority of this host set.
   */
  virtual uint32_t priority() const PURE;

  /**
   * @return uint32_t the overprovisioning factor of this host set.
   */
  virtual uint32_t overprovisioningFactor() const PURE;
};

using HostSetPtr = std::unique_ptr<HostSet>;

/**
 * This class contains all of the HostSets for a given cluster grouped by priority, for
 * ease of load balancing.
 */
class PrioritySet {
public:
  using MemberUpdateCb =
      std::function<void(const HostVector& hosts_added, const HostVector& hosts_removed)>;

  using PriorityUpdateCb = std::function<void(uint32_t priority, const HostVector& hosts_added,
                                              const HostVector& hosts_removed)>;

  virtual ~PrioritySet() = default;

  /**
   * Install a callback that will be invoked when any of the HostSets in the PrioritySet changes.
   * hosts_added and hosts_removed will only be populated when a host is added or completely removed
   * from the PrioritySet.
   * This includes when a new HostSet is created.
   *
   * @param callback supplies the callback to invoke.
   * @return Common::CallbackHandle* a handle which can be used to unregister the callback.
   */
  virtual Common::CallbackHandle* addMemberUpdateCb(MemberUpdateCb callback) const PURE;

  /**
   * Install a callback that will be invoked when a host set changes. Triggers when any change
   * happens to the hosts within the host set. If hosts are added/removed from the host set, the
   * added/removed hosts will be passed to the callback.
   *
   * @param callback supplies the callback to invoke.
   * @return Common::CallbackHandle* a handle which can be used to unregister the callback.
   */
  virtual Common::CallbackHandle* addPriorityUpdateCb(PriorityUpdateCb callback) const PURE;

  /**
   * @return const std::vector<HostSetPtr>& the host sets, ordered by priority.
   */
  virtual const std::vector<HostSetPtr>& hostSetsPerPriority() const PURE;

  /**
   * Parameter class for updateHosts.
   */
  struct UpdateHostsParams {
    HostVectorConstSharedPtr hosts;
    HealthyHostVectorConstSharedPtr healthy_hosts;
    DegradedHostVectorConstSharedPtr degraded_hosts;
    ExcludedHostVectorConstSharedPtr excluded_hosts;
    HostsPerLocalityConstSharedPtr hosts_per_locality;
    HostsPerLocalityConstSharedPtr healthy_hosts_per_locality;
    HostsPerLocalityConstSharedPtr degraded_hosts_per_locality;
    HostsPerLocalityConstSharedPtr excluded_hosts_per_locality;
  };

  /**
   * Updates the hosts in a given host set.
   *
   * @param priority the priority of the host set to update.
   * @param update_hosts_param supplies the list of hosts and hosts per locality.
   * @param locality_weights supplies a map from locality to associated weight.
   * @param hosts_added supplies the hosts added since the last update.
   * @param hosts_removed supplies the hosts removed since the last update.
   * @param overprovisioning_factor if presents, overwrites the current overprovisioning_factor.
   */
  virtual void updateHosts(uint32_t priority, UpdateHostsParams&& update_host_params,
                           LocalityWeightsConstSharedPtr locality_weights,
                           const HostVector& hosts_added, const HostVector& hosts_removed,
                           absl::optional<uint32_t> overprovisioning_factor) PURE;

  /**
   * Callback provided during batch updates that can be used to update hosts.
   */
  class HostUpdateCb {
  public:
    virtual ~HostUpdateCb() = default;
    /**
     * Updates the hosts in a given host set.
     *
     * @param priority the priority of the host set to update.
     * @param update_hosts_param supplies the list of hosts and hosts per locality.
     * @param locality_weights supplies a map from locality to associated weight.
     * @param hosts_added supplies the hosts added since the last update.
     * @param hosts_removed supplies the hosts removed since the last update.
     * @param overprovisioning_factor if presents, overwrites the current overprovisioning_factor.
     */
    virtual void updateHosts(uint32_t priority, UpdateHostsParams&& update_host_params,
                             LocalityWeightsConstSharedPtr locality_weights,
                             const HostVector& hosts_added, const HostVector& hosts_removed,
                             absl::optional<uint32_t> overprovisioning_factor) PURE;
  };

  /**
   * Callback that provides the mechanism for performing batch host updates for a PrioritySet.
   */
  class BatchUpdateCb {
  public:
    virtual ~BatchUpdateCb() = default;

    /**
     * Performs a batch host update. Implementors should use the provided callback to update hosts
     * in the PrioritySet.
     */
    virtual void batchUpdate(HostUpdateCb& host_update_cb) PURE;
  };

  /**
   * Allows updating hosts for multiple priorities at once, deferring the MemberUpdateCb from
   * triggering until all priorities have been updated. The resulting callback will take into
   * account hosts moved from one priority to another.
   *
   * @param callback callback to use to add hosts.
   */
  virtual void batchHostUpdate(BatchUpdateCb& callback) PURE;
};

/**
 * All cluster stats. @see stats_macros.h
 */
#define ALL_CLUSTER_STATS(COUNTER, GAUGE, HISTOGRAM, TEXT_READOUT, STATNAME)                       \
  COUNTER(assignment_stale)                                                                        \
  COUNTER(assignment_timeout_received)                                                             \
  COUNTER(bind_errors)                                                                             \
  COUNTER(lb_healthy_panic)                                                                        \
  COUNTER(lb_local_cluster_not_ok)                                                                 \
  COUNTER(lb_recalculate_zone_structures)                                                          \
  COUNTER(lb_subsets_created)                                                                      \
  COUNTER(lb_subsets_fallback)                                                                     \
  COUNTER(lb_subsets_fallback_panic)                                                               \
  COUNTER(lb_subsets_removed)                                                                      \
  COUNTER(lb_subsets_selected)                                                                     \
  COUNTER(lb_zone_cluster_too_small)                                                               \
  COUNTER(lb_zone_no_capacity_left)                                                                \
  COUNTER(lb_zone_number_differs)                                                                  \
  COUNTER(lb_zone_routing_all_directly)                                                            \
  COUNTER(lb_zone_routing_cross_zone)                                                              \
  COUNTER(lb_zone_routing_sampled)                                                                 \
  COUNTER(membership_change)                                                                       \
  COUNTER(original_dst_host_invalid)                                                               \
  COUNTER(retry_or_shadow_abandoned)                                                               \
  COUNTER(update_attempt)                                                                          \
  COUNTER(update_empty)                                                                            \
  COUNTER(update_failure)                                                                          \
  COUNTER(update_no_rebuild)                                                                       \
  COUNTER(update_success)                                                                          \
  COUNTER(upstream_cx_close_notify)                                                                \
  COUNTER(upstream_cx_connect_attempts_exceeded)                                                   \
  COUNTER(upstream_cx_connect_fail)                                                                \
  COUNTER(upstream_cx_connect_timeout)                                                             \
  COUNTER(upstream_cx_destroy)                                                                     \
  COUNTER(upstream_cx_destroy_local)                                                               \
  COUNTER(upstream_cx_destroy_local_with_active_rq)                                                \
  COUNTER(upstream_cx_destroy_remote)                                                              \
  COUNTER(upstream_cx_destroy_remote_with_active_rq)                                               \
  COUNTER(upstream_cx_destroy_with_active_rq)                                                      \
  COUNTER(upstream_cx_http1_total)                                                                 \
  COUNTER(upstream_cx_http2_total)                                                                 \
  COUNTER(upstream_cx_idle_timeout)                                                                \
  COUNTER(upstream_cx_max_requests)                                                                \
  COUNTER(upstream_cx_none_healthy)                                                                \
  COUNTER(upstream_cx_overflow)                                                                    \
  COUNTER(upstream_cx_pool_overflow)                                                               \
  COUNTER(upstream_cx_protocol_error)                                                              \
  COUNTER(upstream_cx_rx_bytes_total)                                                              \
  COUNTER(upstream_cx_total)                                                                       \
  COUNTER(upstream_cx_tx_bytes_total)                                                              \
  COUNTER(upstream_flow_control_backed_up_total)                                                   \
  COUNTER(upstream_flow_control_drained_total)                                                     \
  COUNTER(upstream_flow_control_paused_reading_total)                                              \
  COUNTER(upstream_flow_control_resumed_reading_total)                                             \
  COUNTER(upstream_internal_redirect_failed_total)                                                 \
  COUNTER(upstream_internal_redirect_succeeded_total)                                              \
  COUNTER(upstream_rq_cancelled)                                                                   \
  COUNTER(upstream_rq_completed)                                                                   \
  COUNTER(upstream_rq_maintenance_mode)                                                            \
  COUNTER(upstream_rq_max_duration_reached)                                                        \
  COUNTER(upstream_rq_pending_failure_eject)                                                       \
  COUNTER(upstream_rq_pending_overflow)                                                            \
  COUNTER(upstream_rq_pending_total)                                                               \
  COUNTER(upstream_rq_per_try_timeout)                                                             \
  COUNTER(upstream_rq_retry)                                                                       \
  COUNTER(upstream_rq_retry_backoff_exponential)                                                   \
  COUNTER(upstream_rq_retry_backoff_ratelimited)                                                   \
  COUNTER(upstream_rq_retry_limit_exceeded)                                                        \
  COUNTER(upstream_rq_retry_overflow)                                                              \
  COUNTER(upstream_rq_retry_success)                                                               \
  COUNTER(upstream_rq_rx_reset)                                                                    \
  COUNTER(upstream_rq_timeout)                                                                     \
  COUNTER(upstream_rq_total)                                                                       \
  COUNTER(upstream_rq_tx_reset)                                                                    \
  GAUGE(lb_subsets_active, Accumulate)                                                             \
  GAUGE(max_host_weight, NeverImport)                                                              \
  GAUGE(membership_degraded, NeverImport)                                                          \
  GAUGE(membership_excluded, NeverImport)                                                          \
  GAUGE(membership_healthy, NeverImport)                                                           \
  GAUGE(membership_total, NeverImport)                                                             \
  GAUGE(upstream_cx_active, Accumulate)                                                            \
  GAUGE(upstream_cx_rx_bytes_buffered, Accumulate)                                                 \
  GAUGE(upstream_cx_tx_bytes_buffered, Accumulate)                                                 \
  GAUGE(upstream_rq_active, Accumulate)                                                            \
  GAUGE(upstream_rq_pending_active, Accumulate)                                                    \
  GAUGE(version, NeverImport)                                                                      \
  HISTOGRAM(upstream_cx_connect_ms, Milliseconds)                                                  \
  HISTOGRAM(upstream_cx_length_ms, Milliseconds)

/**
 * All cluster load report stats. These are only use for EDS load reporting and not sent to the
 * stats sink. See envoy.api.v2.endpoint.ClusterStats for the definition of upstream_rq_dropped.
 * These are latched by LoadStatsReporter, independent of the normal stats sink flushing.
 */
#define ALL_CLUSTER_LOAD_REPORT_STATS(COUNTER, GAUGE, HISTOGRAM, TEXT_READOUT, STATNAME)           \
  COUNTER(upstream_rq_dropped)

/**
 * Cluster circuit breakers gauges. Note that we do not generate a stats
 * structure from this macro. This is because depending on flags, we want to use
 * null gauges for all the "remaining" ones. This is hard to automate with the
 * 2-phase macros, so ClusterInfoImpl::generateCircuitBreakersStats is
 * hand-coded and must be changed if we alter the set of gauges in this macro.
 * We also include stat-names in this structure that are used when composing
 * the circuit breaker names, depending on priority settings.
 */
#define ALL_CLUSTER_CIRCUIT_BREAKERS_STATS(COUNTER, GAUGE, HISTOGRAM, TEXT_READOUT, STATNAME)      \
  GAUGE(cx_open, Accumulate)                                                                       \
  GAUGE(cx_pool_open, Accumulate)                                                                  \
  GAUGE(rq_open, Accumulate)                                                                       \
  GAUGE(rq_pending_open, Accumulate)                                                               \
  GAUGE(rq_retry_open, Accumulate)                                                                 \
  GAUGE(remaining_cx, Accumulate)                                                                  \
  GAUGE(remaining_cx_pools, Accumulate)                                                            \
  GAUGE(remaining_pending, Accumulate)                                                             \
  GAUGE(remaining_retries, Accumulate)                                                             \
  GAUGE(remaining_rq, Accumulate)                                                                  \
  STATNAME(circuit_breakers)                                                                       \
  STATNAME(default)                                                                                \
  STATNAME(high)

/**
 * All stats tracking request/response headers and body sizes. Not used by default.
 */
#define ALL_CLUSTER_REQUEST_RESPONSE_SIZE_STATS(COUNTER, GAUGE, HISTOGRAM, TEXT_READOUT, STATNAME) \
  HISTOGRAM(upstream_rq_headers_size, Bytes)                                                       \
  HISTOGRAM(upstream_rq_body_size, Bytes)                                                          \
  HISTOGRAM(upstream_rs_headers_size, Bytes)                                                       \
  HISTOGRAM(upstream_rs_body_size, Bytes)

/**
 * All stats around timeout budgets. Not used by default.
 */
#define ALL_CLUSTER_TIMEOUT_BUDGET_STATS(COUNTER, GAUGE, HISTOGRAM, TEXT_READOUT, STATNAME)        \
  HISTOGRAM(upstream_rq_timeout_budget_percent_used, Unspecified)                                  \
  HISTOGRAM(upstream_rq_timeout_budget_per_try_percent_used, Unspecified)

/**
 * Struct definition for all cluster stats. @see stats_macros.h
 */
MAKE_STAT_NAMES_STRUCT(ClusterStatNames, ALL_CLUSTER_STATS);
MAKE_STATS_STRUCT(ClusterStats, ClusterStatNames, ALL_CLUSTER_STATS);

MAKE_STAT_NAMES_STRUCT(ClusterLoadReportStatNames, ALL_CLUSTER_LOAD_REPORT_STATS);
MAKE_STATS_STRUCT(ClusterLoadReportStats, ClusterLoadReportStatNames,
                  ALL_CLUSTER_LOAD_REPORT_STATS);

// We can't use macros to make the Stats class for circuit breakers due to
// the conditional inclusion of 'remaining' gauges. But we do auto-generate
// the StatNames struct.
MAKE_STAT_NAMES_STRUCT(ClusterCircuitBreakersStatNames, ALL_CLUSTER_CIRCUIT_BREAKERS_STATS);

MAKE_STAT_NAMES_STRUCT(ClusterRequestResponseSizeStatNames,
                       ALL_CLUSTER_REQUEST_RESPONSE_SIZE_STATS);
MAKE_STATS_STRUCT(ClusterRequestResponseSizeStats, ClusterRequestResponseSizeStatNames,
                  ALL_CLUSTER_REQUEST_RESPONSE_SIZE_STATS);

MAKE_STAT_NAMES_STRUCT(ClusterTimeoutBudgetStatNames, ALL_CLUSTER_TIMEOUT_BUDGET_STATS);
MAKE_STATS_STRUCT(ClusterTimeoutBudgetStats, ClusterTimeoutBudgetStatNames,
                  ALL_CLUSTER_TIMEOUT_BUDGET_STATS);

/**
 * Struct definition for cluster circuit breakers stats. @see stats_macros.h
 */
struct ClusterCircuitBreakersStats {
  ALL_CLUSTER_CIRCUIT_BREAKERS_STATS(c, GENERATE_GAUGE_STRUCT, h, tr, GENERATE_STATNAME_STRUCT)
};

using ClusterRequestResponseSizeStatsPtr = std::unique_ptr<ClusterRequestResponseSizeStats>;
using ClusterRequestResponseSizeStatsOptRef =
    absl::optional<std::reference_wrapper<ClusterRequestResponseSizeStats>>;

using ClusterTimeoutBudgetStatsPtr = std::unique_ptr<ClusterTimeoutBudgetStats>;
using ClusterTimeoutBudgetStatsOptRef =
    absl::optional<std::reference_wrapper<ClusterTimeoutBudgetStats>>;

/**
 * All extension protocol specific options returned by the method at
 *   NamedNetworkFilterConfigFactory::createProtocolOptions
 * must be derived from this class.
 */
class ProtocolOptionsConfig {
public:
  virtual ~ProtocolOptionsConfig() = default;
};
using ProtocolOptionsConfigConstSharedPtr = std::shared_ptr<const ProtocolOptionsConfig>;

/**
 *  Base class for all cluster typed metadata factory.
 */
class ClusterTypedMetadataFactory : public Envoy::Config::TypedMetadataFactory {};

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
    // Whether connections should be immediately closed upon health failure.
    static const uint64_t CLOSE_CONNECTIONS_ON_HOST_HEALTH_FAILURE = 0x4;
    // If USE_ALPN and HTTP2 are true, the upstream protocol will be negotiated using ALPN.
    // If ALPN is attempted but not supported by the upstream HTTP/1.1 is used.
    static const uint64_t USE_ALPN = 0x8;
  };

  virtual ~ClusterInfo() = default;

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
   * @return the idle timeout for upstream connection pool connections.
   */
  virtual const absl::optional<std::chrono::milliseconds> idleTimeout() const PURE;

  /**
   * @return how many streams should be anticipated per each current stream.
   */
  virtual float perUpstreamPreconnectRatio() const PURE;

  /**
   * @return how many streams should be anticipated per each current stream.
   */
  virtual float peekaheadRatio() const PURE;

  /**
   * @return soft limit on size of the cluster's connections read and write buffers.
   */
  virtual uint32_t perConnectionBufferLimitBytes() const PURE;

  /**
   * @return uint64_t features supported by the cluster. @see Features.
   */
  virtual uint64_t features() const PURE;

  /**
   * @return const Http::Http1Settings& for HTTP/1.1 connections created on behalf of this cluster.
   *         @see Http::Http1Settings.
   */
  virtual const Http::Http1Settings& http1Settings() const PURE;

  /**
   * @return const envoy::config::core::v3::Http2ProtocolOptions& for HTTP/2 connections
   * created on behalf of this cluster.
   *         @see envoy::config::core::v3::Http2ProtocolOptions.
   */
  virtual const envoy::config::core::v3::Http2ProtocolOptions& http2Options() const PURE;

  /**
   * @return const envoy::config::core::v3::HttpProtocolOptions for all of HTTP versions.
   */
  virtual const envoy::config::core::v3::HttpProtocolOptions&
  commonHttpProtocolOptions() const PURE;

  /**
   * @param name std::string containing the well-known name of the extension for which protocol
   *        options are desired
   * @return std::shared_ptr<const Derived> where Derived is a subclass of ProtocolOptionsConfig
   *         and contains extension-specific protocol options for upstream connections.
   */
  template <class Derived>
  std::shared_ptr<const Derived> extensionProtocolOptionsTyped(const std::string& name) const {
    return std::dynamic_pointer_cast<const Derived>(extensionProtocolOptions(name));
  }

  /**
   * @return const envoy::config::cluster::v3::Cluster::CommonLbConfig& the common configuration for
   * all load balancers for this cluster.
   */
  virtual const envoy::config::cluster::v3::Cluster::CommonLbConfig& lbConfig() const PURE;

  /**
   * @return the type of load balancing that the cluster should use.
   */
  virtual LoadBalancerType lbType() const PURE;

  /**
   * @return the service discovery type to use for resolving the cluster.
   */
  virtual envoy::config::cluster::v3::Cluster::DiscoveryType type() const PURE;

  /**
   * @return the type of cluster, only used for custom discovery types.
   */
  virtual const absl::optional<envoy::config::cluster::v3::Cluster::CustomClusterType>&
  clusterType() const PURE;

  /**
   * @return configuration for least request load balancing, only used if LB type is least request.
   */
  virtual const absl::optional<envoy::config::cluster::v3::Cluster::LeastRequestLbConfig>&
  lbLeastRequestConfig() const PURE;

  /**
   * @return configuration for ring hash load balancing, only used if type is set to ring_hash_lb.
   */
  virtual const absl::optional<envoy::config::cluster::v3::Cluster::RingHashLbConfig>&
  lbRingHashConfig() const PURE;

  /**
   * @return configuration for maglev load balancing, only used if type is set to maglev_lb.
   */
  virtual const absl::optional<envoy::config::cluster::v3::Cluster::MaglevLbConfig>&
  lbMaglevConfig() const PURE;

  /**
   * @return const absl::optional<envoy::config::cluster::v3::Cluster::OriginalDstLbConfig>& the
   * configuration for the Original Destination load balancing policy, only used if type is set to
   *         ORIGINAL_DST_LB.
   */
  virtual const absl::optional<envoy::config::cluster::v3::Cluster::OriginalDstLbConfig>&
  lbOriginalDstConfig() const PURE;

  /**
   * @return const absl::optional<envoy::config::core::v3::TypedExtensionConfig>& the configuration
   *         for the upstream, if a custom upstream is configured.
   */
  virtual const absl::optional<envoy::config::core::v3::TypedExtensionConfig>&
  upstreamConfig() const PURE;

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
   * @return uint32_t the maximum number of response headers. The default value is 100. Results in a
   * reset if the number of headers exceeds this value.
   */
  virtual uint32_t maxResponseHeadersCount() const PURE;

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
   * @return TransportSocketMatcher& the transport socket matcher associated
   * factory.
   */
  virtual TransportSocketMatcher& transportSocketMatcher() const PURE;

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
   * @return absl::optional<std::reference_wrapper<ClusterRequestResponseSizeStats>> stats to track
   * headers/body sizes of request/response for this cluster.
   */
  virtual ClusterRequestResponseSizeStatsOptRef requestResponseSizeStats() const PURE;

  /**
   * @return absl::optional<std::reference_wrapper<ClusterTimeoutBudgetStats>> stats on timeout
   * budgets for this cluster.
   */
  virtual ClusterTimeoutBudgetStatsOptRef timeoutBudgetStats() const PURE;

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
   * @return const envoy::config::core::v3::Metadata& the configuration metadata for this cluster.
   */
  virtual const envoy::config::core::v3::Metadata& metadata() const PURE;

  /**
   * @return const Envoy::Config::TypedMetadata&& the typed metadata for this cluster.
   */
  virtual const Envoy::Config::TypedMetadata& typedMetadata() const PURE;

  /**
   *
   * @return const Network::ConnectionSocket::OptionsSharedPtr& socket options for all
   *         connections for this cluster.
   */
  virtual const Network::ConnectionSocket::OptionsSharedPtr& clusterSocketOptions() const PURE;

  /**
   * @return whether to skip waiting for health checking before draining connections
   *         after a host is removed from service discovery.
   */
  virtual bool drainConnectionsOnHostRemoval() const PURE;

  /**
   *  @return whether to create a new connection pool for each downstream connection routed to
   *          the cluster
   */
  virtual bool connectionPoolPerDownstreamConnection() const PURE;

  /**
   * @return true if this cluster is configured to ignore hosts for the purpose of load balancing
   * computations until they have been health checked for the first time.
   */
  virtual bool warmHosts() const PURE;

  /**
   * @return eds cluster service_name of the cluster.
   */
  virtual absl::optional<std::string> edsServiceName() const PURE;

  /**
   * Create network filters on a new upstream connection.
   */
  virtual void createNetworkFilterChain(Network::Connection& connection) const PURE;

  /**
   * Calculate upstream protocol(s) based on features.
   */
  virtual std::vector<Http::Protocol>
  upstreamHttpProtocol(absl::optional<Http::Protocol> downstream_protocol) const PURE;

  /**
   * @return http protocol options for upstream connection
   */
  virtual const absl::optional<envoy::config::core::v3::UpstreamHttpProtocolOptions>&
  upstreamHttpProtocolOptions() const PURE;

  /**
   * @return the Http1 Codec Stats.
   */
  virtual Http::Http1::CodecStats& http1CodecStats() const PURE;

  /**
   * @return the Http2 Codec Stats.
   */
  virtual Http::Http2::CodecStats& http2CodecStats() const PURE;

protected:
  /**
   * Invoked by extensionProtocolOptionsTyped.
   * @param name std::string containing the well-known name of the extension for which protocol
   *        options are desired
   * @return ProtocolOptionsConfigConstSharedPtr with extension-specific protocol options for
   *         upstream connections.
   */
  virtual ProtocolOptionsConfigConstSharedPtr
  extensionProtocolOptions(const std::string& name) const PURE;
};

using ClusterInfoConstSharedPtr = std::shared_ptr<const ClusterInfo>;

class HealthChecker;

/**
 * An upstream cluster (group of hosts). This class is the "primary" singleton cluster used amongst
 * all forwarding threads/workers. Individual HostSets are used on the workers themselves.
 */
class Cluster {
public:
  virtual ~Cluster() = default;

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
   *         clusters that depend on other clusters can correctly initialize. (E.g., an EDS cluster
   *         that depends on resolution of the EDS server itself).
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

using ClusterSharedPtr = std::shared_ptr<Cluster>;
using ClusterConstOptRef = absl::optional<std::reference_wrapper<const Cluster>>;

} // namespace Upstream
} // namespace Envoy
