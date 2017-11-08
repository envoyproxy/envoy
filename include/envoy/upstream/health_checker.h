#pragma once

#include <functional>
#include <memory>

#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Upstream {

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
  typedef std::function<void(HostSharedPtr host, bool changed_state)> HostStatusCb;

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

} // namespace Upstream
} // namespace Envoy
