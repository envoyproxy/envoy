#pragma once

#include <functional>
#include <memory>

#include "envoy/data/core/v2alpha/health_check_event.pb.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Upstream {

enum class HealthState { Unhealthy, Healthy };

enum class HealthTransition {
  /**
   * Used when the health state of a host hasn't changed.
   */
  Unchanged,
  /**
   * Used when the health state of a host has changed.
   */
  Changed,
  /**
   * Used when the health check result differs from the health state of a host, but a change to the
   * latter is delayed due to healthy/unhealthy threshold settings.
   */
  ChangePending
};

/**
 * Wraps active health checking of an upstream cluster.
 */
class HealthChecker {
public:
  virtual ~HealthChecker() {}

  /**
   * Called when a host has been health checked.
   * @param host supplies the host that was just health checked.
   * @param changed_state supplies whether the health check resulted in a host moving from healthy
   *                       to not healthy or vice versa.
   */
  typedef std::function<void(HostSharedPtr host, HealthTransition changed_state)> HostStatusCb;

  /**
   * Install a callback that will be invoked every time a health check round is completed for
   * a host. The host's health check state may not have changed.
   * @param callback supplies the callback to invoke.
   */
  virtual void addHostCheckCompleteCb(HostStatusCb callback) PURE;

  /**
   * Start cyclic health checking based on the provided settings and the type of health checker.
   */
  virtual void start() PURE;
};

typedef std::shared_ptr<HealthChecker> HealthCheckerSharedPtr;

std::ostream& operator<<(std::ostream& out, HealthState state);
std::ostream& operator<<(std::ostream& out, HealthTransition changed_state);

/**
 * Sink for health check event logs.
 */
class HealthCheckEventLogger {
public:
  virtual ~HealthCheckEventLogger() {}

  /**
   * Log an unhealthy host ejection event.
   * @param health_checker_type supplies the type of health checker that generated the event.
   * @param host supplies the host that generated the event.
   * @param failure_type supplies the type of health check failure
   */
  virtual void
  logEjectUnhealthy(envoy::data::core::v2alpha::HealthCheckerType health_checker_type,
                    const HostDescriptionConstSharedPtr& host,
                    envoy::data::core::v2alpha::HealthCheckFailureType failure_type) PURE;

  /**
   * Log a healthy host addition event.
   * @param health_checker_type supplies the type of health checker that generated the event.
   * @param host supplies the host that generated the event.
   * @param healthy_threshold supplied the configured healthy threshold for this health check
   * @param first_check whether this is a fast path success on the first health check for this host
   */
  virtual void logAddHealthy(envoy::data::core::v2alpha::HealthCheckerType health_checker_type,
                             const HostDescriptionConstSharedPtr& host, bool first_check) PURE;
};

typedef std::unique_ptr<HealthCheckEventLogger> HealthCheckEventLoggerPtr;

} // namespace Upstream
} // namespace Envoy
