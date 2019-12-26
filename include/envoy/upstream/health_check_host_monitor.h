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
   * Mark the host as unhealthy. Note that this may not be immediate as events may need to be
   * propagated between multiple threads.
   */
  virtual void setUnhealthy() PURE;
};

using HealthCheckHostMonitorPtr = std::unique_ptr<HealthCheckHostMonitor>;

} // namespace Upstream
} // namespace Envoy
