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
 * in addition to periodic monitoring on the main thread, and receives immediate feedback
 * when load is admitted.
 *
 * Choosing between ProactiveResourceMonitor and SynchronousFeedbackResourceMonitor:
 * - Use ProactiveResourceMonitor when tracking a discrete, bounded resource with a well-defined
 *   lifecycle where units can be explicitly reserved on creation and released on teardown
 *   (e.g., active downstream connections via `tryAllocateResource()` / `tryDeallocateResource()`).
 * - Use SynchronousFeedbackResourceMonitor when shedding load via LoadShedPoints based on
 *   continuous pressure (threshold or scaled triggers), where the monitor needs to be evaluated
 *   synchronously on the worker hot path and update its pressure estimate immediately as work is
 *   admitted (e.g., adjusting rate counters, token buckets, or estimated CPU/memory headroom
 *   between background refreshes), without tracking when that work finishes.
 *
 * Implementations MUST be thread-safe, as getResourceUsage() and onLoadAccepted() are
 * invoked concurrently across worker threads (and the main thread) without external
 * synchronization.
 */
class SynchronousFeedbackResourceMonitor : public ResourceMonitor {
public:
  ~SynchronousFeedbackResourceMonitor() override = default;

  /**
   * Synchronously returns the current resource usage.
   * Called directly by LoadShedPoints on worker threads (and by the default
   * updateResourceUsage() implementation on the main thread). Must be thread-safe.
   */
  virtual ResourceUsage getResourceUsage() = 0;

  /**
   * Allows SynchronousFeedbackResourceMonitor to also be used with OverloadActions and periodic
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

using SynchronousFeedbackResourceMonitorPtr = std::unique_ptr<SynchronousFeedbackResourceMonitor>;
using SynchronousFeedbackResourceMonitorSharedPtr =
    std::shared_ptr<SynchronousFeedbackResourceMonitor>;

} // namespace Server
} // namespace Envoy
