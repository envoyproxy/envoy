#pragma once

#include <memory>
#include <string>

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/network/address.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/health_check_host_monitor.h"
#include "envoy/upstream/outlier_detection.h"

namespace Envoy {
namespace Upstream {

/**
 * All per host stats. @see stats_macros.h
 *
 * {rq_success, rq_error} have specific semantics driven by the needs of EDS load reporting. See
 * envoy.api.v2.endpoint.UpstreamLocalityStats for the definitions of success/error. These are
 * latched by LoadStatsReporter, independent of the normal stats sink flushing.
 */
// clang-format off
#define ALL_HOST_STATS(COUNTER, GAUGE)                                                             \
  COUNTER(cx_total)                                                                                \
  GAUGE  (cx_active)                                                                               \
  COUNTER(cx_connect_fail)                                                                         \
  COUNTER(rq_total)                                                                                \
  COUNTER(rq_timeout)                                                                              \
  COUNTER(rq_success)                                                                              \
  COUNTER(rq_error)                                                                                \
  GAUGE  (rq_active)
// clang-format on

/**
 * All per host stats defined. @see stats_macros.h
 */
struct HostStats {
  ALL_HOST_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

class ClusterInfo;

/**
 * A description of an upstream host.
 */
class HostDescription {
public:
  virtual ~HostDescription() {}

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
  virtual const HostStats& stats() const PURE;

  /**
   * @return the locality of the host (deployment specific). This will be the default instance if
   *         unknown.
   */
  virtual const envoy::api::v2::core::Locality& locality() const PURE;

  /**
   * @return the address used to health check the host.
   */
  virtual Network::Address::InstanceConstSharedPtr healthCheckAddress() const PURE;
};

typedef std::shared_ptr<const HostDescription> HostDescriptionConstSharedPtr;

} // namespace Upstream
} // namespace Envoy
