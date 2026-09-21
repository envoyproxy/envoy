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
using ResourceMonitorSharedPtr = std::shared_ptr<ResourceMonitor>;

/**
 * A resource monitor that can be queried synchronously by LoadShedPoints on worker threads
 * in addition to periodic monitoring on the main thread.
 *
 * Implementations MUST be thread-safe, as getResourceUsage() and onLoadAccepted() are
 * invoked concurrently across worker threads (and the main thread) without external
 * synchronization.
 */
class RealtimeResourceMonitor : public ResourceMonitor {
public:
  ~RealtimeResourceMonitor() override = default;

  /**
   * Synchronously returns the current resource usage.
   * Called directly by LoadShedPoints on worker threads (and by the default
   * updateResourceUsage() implementation on the main thread). Must be thread-safe.
   */
  virtual ResourceUsage getResourceUsage() = 0;

  /**
   * Allows RealtimeResourceMonitor to also be used with OverloadActions and periodic
   * pressure stats. Always called on the main thread.
   */
  void updateResourceUsage(ResourceUpdateCallbacks& callbacks) override {
    callbacks.onSuccess(getResourceUsage());
  }

  /**
   * Optional callback invoked synchronously on a worker thread when a LoadShedPoint
   * check passes and load is accepted. Must be thread-safe.
   */
  virtual void onLoadAccepted(absl::string_view load_shed_point_name) {
    UNREFERENCED_PARAMETER(load_shed_point_name);
  }
};

using RealtimeResourceMonitorPtr = std::unique_ptr<RealtimeResourceMonitor>;
using RealtimeResourceMonitorSharedPtr = std::shared_ptr<RealtimeResourceMonitor>;

} // namespace Server
} // namespace Envoy
