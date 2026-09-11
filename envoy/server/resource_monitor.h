#pragma once

#include <memory>

#include "envoy/common/pure.h"

#include "source/common/common/assert.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Server {

// Struct for reporting usage for a particular resource.
struct ResourceUsage {
  bool operator==(const ResourceUsage& rhs) const {
    return resource_pressure_ == rhs.resource_pressure_;
  }

  // Fraction of (resource usage)/(resource limit).
  double resource_pressure_;
};

/**
 * Notifies caller of updated resource usage.
 */
class ResourceUpdateCallbacks {
public:
  virtual ~ResourceUpdateCallbacks() = default;

  /**
   * Called when the request for updated resource usage succeeds.
   * @param usage the updated resource usage
   */
  virtual void onSuccess(const ResourceUsage& usage) PURE;

  /**
   * Called when the request for updated resource usage fails.
   * @param error the status describing the failure
   */
  virtual void onFailure(const absl::Status& error) PURE;
};

class ResourceMonitor {
public:
  virtual ~ResourceMonitor() = default;

  /**
   * Recalculate resource usage.
   * This must be non-blocking so if RPCs need to be made they should be
   * done asynchronously and invoke the callback when finished.
   */
  virtual void updateResourceUsage(ResourceUpdateCallbacks& callbacks) PURE;
};

using ResourceMonitorPtr = std::unique_ptr<ResourceMonitor>;

} // namespace Server
} // namespace Envoy
