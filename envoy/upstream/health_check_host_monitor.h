#pragma once

#include <memory>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Upstream {

/**
 * A monitor for "passive" health check events that might happen on every thread. For example, if a
 * special HTTP header is received, the data plane may decide to fast fail a host to avoid waiting
 * for the full Health Check interval to elapse before determining the host is active health check
 * failed.
 */
class HealthCheckHostMonitor {
public:
  virtual ~HealthCheckHostMonitor() = default;

  /**
   * The reason the host is being set unhealthy via the monitor.
   */
  enum class UnhealthyType {
    // Protocol indication (e.g., x-envoy-immediate-health-check-fail) that the host should be
    // immediately taken out of rotation.
    ImmediateHealthCheckFail
  };

  /**
   * Mark the host as unhealthy. Note that this may not be immediate as events may need to be
   * propagated between multiple threads.
   * @param type specifies the reason the host is being marked unhealthy.
   */
  virtual void setUnhealthy(UnhealthyType type) PURE;
};

using HealthCheckHostMonitorPtr = std::unique_ptr<HealthCheckHostMonitor>;

} // namespace Upstream
} // namespace Envoy
