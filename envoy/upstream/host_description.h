#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/time.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/address.h"
#include "envoy/network/transport_socket.h"
#include "envoy/stats/primitive_stats_macros.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/health_check_host_monitor.h"
#include "envoy/upstream/outlier_detection.h"
#include "envoy/upstream/resource_manager.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Upstream {

using MetadataConstSharedPtr = std::shared_ptr<const envoy::config::core::v3::Metadata>;

/**
 * All per host stats. @see stats_macros.h
 *
 * {rq_success, rq_error} have specific semantics driven by the needs of EDS load reporting. See
 * envoy.api.v2.endpoint.UpstreamLocalityStats for the definitions of success/error. These are
 * latched by LoadStatsReporter, independent of the normal stats sink flushing.
 */
#define ALL_HOST_STATS(COUNTER, GAUGE)                                                             \
  COUNTER(cx_connect_fail)                                                                         \
  COUNTER(cx_total)                                                                                \
  COUNTER(rq_error)                                                                                \
  COUNTER(rq_success)                                                                              \
  COUNTER(rq_timeout)                                                                              \
  COUNTER(rq_total)                                                                                \
  GAUGE(cx_active)                                                                                 \
  GAUGE(rq_active)

/**
 * All per host stats defined. @see stats_macros.h
 */
struct HostStats {
  ALL_HOST_STATS(GENERATE_PRIMITIVE_COUNTER_STRUCT, GENERATE_PRIMITIVE_GAUGE_STRUCT);

  // Provide access to name,counter pairs.
  std::vector<std::pair<absl::string_view, Stats::PrimitiveCounterReference>> counters() {
    return {ALL_HOST_STATS(PRIMITIVE_COUNTER_NAME_AND_REFERENCE, IGNORE_PRIMITIVE_GAUGE)};
  }

  // Provide access to name,gauge pairs.
  std::vector<std::pair<absl::string_view, Stats::PrimitiveGaugeReference>> gauges() {
    return {ALL_HOST_STATS(IGNORE_PRIMITIVE_COUNTER, PRIMITIVE_GAUGE_NAME_AND_REFERENCE)};
  }
};

/**
 * Weakly-named load metrics to be reported as EndpointLoadMetricStats. Individual stats are
 * accumulated by calling add(), which combines stats with the same name. The aggregated stats are
 * retrieved by calling latch(), which also clears the current load metrics.
 */
class LoadMetricStats {
public:
  virtual ~LoadMetricStats() = default;

  struct Stat {
    uint64_t num_requests_with_metric = 0;
    double total_metric_value = 0.0;
  };

  using StatMap = absl::flat_hash_map<std::string, Stat>;
  using StatMapPtr = std::unique_ptr<StatMap>;

  // Adds the given stat to the map. If the stat already exists in the map, then the stat is
  // combined with the existing map entry by incrementing num_requests_with_metric and summing the
  // total_metric_value fields. Otherwise, the stat is added with the provided value to the map,
  // which retains all entries until the next call to latch(). This allows metrics to be added
  // whose keys may not necessarily be known at startup time.
  virtual void add(const absl::string_view key, double value) PURE;

  // Returns an owning pointer to the current load metrics and clears the map.
  virtual StatMapPtr latch() PURE;
};

class ClusterInfo;

/**
 * A description of an upstream host.
 */
class HostDescription {
public:
  virtual ~HostDescription() = default;

  /**
   * @return whether the host is a canary.
   */
  virtual bool canary() const PURE;

  /**
   * Update the canary status of the host.
   */
  virtual void canary(bool is_canary) PURE;

  /**
   * @return the metadata associated with this host
   */
  virtual MetadataConstSharedPtr metadata() const PURE;

  /**
   * Set the current metadata.
   */
  virtual void metadata(MetadataConstSharedPtr new_metadata) PURE;

  /**
   * @return the cluster the host is a member of.
   */
  virtual const ClusterInfo& cluster() const PURE;

  /**
   * @return true if the cluster can create a connection for this priority, false otherwise.
   * @param priority the priority the connection would have.
   */
  virtual bool canCreateConnection(Upstream::ResourcePriority priority) const PURE;

  /**
   * @return the host's outlier detection monitor.
   */
  virtual Outlier::DetectorHostMonitor& outlierDetector() const PURE;

  /**
   * @return the host's health checker monitor.
   */
  virtual HealthCheckHostMonitor& healthChecker() const PURE;

  /**
   * @return The hostname used as the host header for health checking.
   */
  virtual const std::string& hostnameForHealthChecks() const PURE;

  /**
   * @return the hostname associated with the host if any.
   * Empty string "" indicates that hostname is not a DNS name.
   */
  virtual const std::string& hostname() const PURE;

  /**
   * @return the transport socket factory responsible for this host.
   */
  virtual Network::UpstreamTransportSocketFactory& transportSocketFactory() const PURE;

  /**
   * @return the address used to connect to the host.
   */
  virtual Network::Address::InstanceConstSharedPtr address() const PURE;

  /**
   * @return a optional list of additional addresses which the host resolved to. These addresses
   *         may be used to create upstream connections if the primary address is unreachable.
   */
  virtual const std::vector<Network::Address::InstanceConstSharedPtr>& addressList() const PURE;

  /**
   * @return host specific stats.
   */
  virtual HostStats& stats() const PURE;

  /**
   * @return custom stats for multi-dimensional load balancing.
   */
  virtual LoadMetricStats& loadMetricStats() const PURE;

  /**
   * @return the locality of the host (deployment specific). This will be the default instance if
   *         unknown.
   */
  virtual const envoy::config::core::v3::Locality& locality() const PURE;

  /**
   * @return the human readable name of the host's locality zone as a StatName.
   */
  virtual Stats::StatName localityZoneStatName() const PURE;

  /**
   * @return the address used to health check the host.
   */
  virtual Network::Address::InstanceConstSharedPtr healthCheckAddress() const PURE;

  /**
   * @return the priority of the host.
   */
  virtual uint32_t priority() const PURE;

  /**
   * Set the current priority.
   */
  virtual void priority(uint32_t) PURE;

  /**
   * @return timestamp of when host has transitioned from unhealthy to
   *         healthy state via an active healthchecking.
   */
  virtual absl::optional<MonotonicTime> lastHcPassTime() const PURE;
};

using HostDescriptionConstSharedPtr = std::shared_ptr<const HostDescription>;

#define ALL_TRANSPORT_SOCKET_MATCH_STATS(COUNTER) COUNTER(total_match_count)

/**
 * The stats for transport socket match.
 */
struct TransportSocketMatchStats {
  ALL_TRANSPORT_SOCKET_MATCH_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Library to determine what transport socket configuration to use for a given host.
 */
class TransportSocketMatcher {
public:
  struct MatchData {
    MatchData(Network::UpstreamTransportSocketFactory& factory, TransportSocketMatchStats& stats,
              std::string name)
        : factory_(factory), stats_(stats), name_(std::move(name)) {}
    Network::UpstreamTransportSocketFactory& factory_;
    TransportSocketMatchStats& stats_;
    std::string name_;
  };
  virtual ~TransportSocketMatcher() = default;

  /**
   * Resolve the transport socket configuration for a particular host.
   * @param metadata the metadata of the given host.
   * @return the match information of the transport socket selected.
   */
  virtual MatchData resolve(const envoy::config::core::v3::Metadata* metadata) const PURE;

  /*
   * return true if all matches support ALPN, false otherwise.
   */
  virtual bool allMatchesSupportAlpn() const PURE;
};

using TransportSocketMatcherPtr = std::unique_ptr<TransportSocketMatcher>;

} // namespace Upstream
} // namespace Envoy
