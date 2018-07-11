#pragma once

#include <functional>
#include <memory>

#include "envoy/common/exception.h"
#include "envoy/common/pure.h"

namespace Envoy {
namespace Server {

// Struct for reporting usage for a particular resource.
struct ResourceUsage {
  // Fraction of (resource usage)/(resource limit).
  double resource_pressure_;
};

class ResourceMonitor {
public:
  virtual ~ResourceMonitor() {}

  // Callback for handling updated usage information for this resource.
  // Exactly one of 'usage' or 'error' will be non-null.
  typedef std::function<void(const ResourceUsage* usage, const EnvoyException* error)> UpdateCb;

  /**
   * Recalculate resource usage.
   * This must be non-blocking so if RPCs need to be made they should be
   * done asynchronously and invoke the callback when finished.
   */
  virtual void updateResourceUsage(const UpdateCb& completionCb) PURE;
};

typedef std::unique_ptr<ResourceMonitor> ResourceMonitorPtr;

} // namespace Server
} // namespace Envoy
