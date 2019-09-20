#pragma once

#include <map>
#include <memory>
#include <string>

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/network/address.h"
#include "envoy/stats/primitive_stats_macros.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/health_check_host_monitor.h"
#include "envoy/upstream/outlier_detection.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Upstream {

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
  std::vector<std::pair<absl::string_view, std::reference_wrapper<const Stats::PrimitiveCounter>>>
  counters() const {
    return {ALL_HOST_STATS(PRIMITIVE_COUNTER_NAME_AND_REFERENCE, IGNORE_PRIMITIVE_GAUGE)};
  }

  // Provide access to name,gauge pairs.
  std::vector<std::pair<absl::string_view, std::reference_wrapper<const Stats::PrimitiveGauge>>>
  gauges() const {
    return {ALL_HOST_STATS(IGNORE_PRIMITIVE_COUNTER, PRIMITIVE_GAUGE_NAME_AND_REFERENCE)};
  }
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
  virtual const std::shared_ptr<envoy::api::v2::core::Metadata> metadata() const PURE;

  /**
   * Set the current metadata.
   */
  virtual void metadata(const envoy::api::v2::core::Metadata& new_metadata) PURE;

  /**
   * @return the cluster the host is a member of.
   */
  virtual const ClusterInfo& cluster() const PURE;

  /**
   * @return the host's outlier detection monitor.
   */
  virtual Outlier::DetectorHostMonitor& outlierDetector() const PURE;

  /**
   * @return the host's health checker monitor.
   */
  virtual HealthCheckHostMonitor& healthChecker() const PURE;

  /**
   * @return the hostname associated with the host if any.
   * Empty string "" indicates that hostname is not a DNS name.
   */
  virtual const std::string& hostname() const PURE;

  /**
   * @return the address used to connect to the host.
   */
  virtual Network::Address::InstanceConstSharedPtr address() const PURE;

  /**
   * @return host specific stats.
   */
  virtual HostStats& stats() const PURE;

  /**
   * @return the locality of the host (deployment specific). This will be the default instance if
   *         unknown.
   */
  virtual const envoy::api::v2::core::Locality& locality() const PURE;

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
};

using HostDescriptionConstSharedPtr = std::shared_ptr<const HostDescription>;

} // namespace Upstream
} // namespace Envoy
