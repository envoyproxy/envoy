#pragma once

#include <memory>

#include "envoy/common/exception.h"
#include "envoy/common/pure.h"
#include "envoy/event/dispatcher.h"

#include "common/common/assert.h"

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
   * @param error the exception caught when trying to get updated resource usage
   */
  virtual void onFailure(const EnvoyException& error) PURE;
};

class ReactiveResourceUpdateCallbacks {
public:
  virtual ~ReactiveResourceUpdateCallbacks() = default;

  /**
   * Called when the request for updated resource usage succeeds.
   * @param usage the updated resource usage
   */
  virtual void onSuccess(const uint64_t usage) PURE;

  /**
   * Called when the request for updated resource usage fails.
   * todo may need propagate error
   */
  virtual void onFailure() PURE;

  virtual Event::Dispatcher& dispatcher() PURE;
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

class ReactiveResourceMonitor {
public:
  ReactiveResourceMonitor() = default;
  virtual ~ReactiveResourceMonitor() = default;
  virtual bool tryAllocateResource(uint64_t increment) PURE;
  virtual bool tryDeallocateResource(uint64_t decrement) PURE;
  virtual uint64_t currentResourceUsage() const PURE;
  virtual uint64_t maxResourceUsage() const PURE;
};

using ResourceMonitorPtr = std::unique_ptr<ResourceMonitor>;

using ReactiveResourceMonitorPtr = std::unique_ptr<ReactiveResourceMonitor>;

// Example of reactive resource monitor. To be removed.
class ActiveConnectionsResourceMonitor : public ReactiveResourceMonitor {
public:
  ActiveConnectionsResourceMonitor(uint64_t max_active_conns)
      : max_(max_active_conns), current_(0){};

  bool tryAllocateResource(uint64_t increment) {
    auto curr_capacity = current_.load();
    if (std::numeric_limits<uint64_t>::max() - increment < curr_capacity) {
      // uint64_t overflow, cannot allocate resource.
      return false;
    } else {
      current_ += increment;
      return true;
    }
  }

  bool tryDeallocateResource(uint64_t decrement) {
    auto curr_capacity = current_.load();
    if (decrement > curr_capacity) {
      // There are not enough resources to deallocate.
      return false;
    } else {
      current_ -= decrement;
      return true;
    }
  }

  uint64_t currentResourceUsage() const { return current_.load(); }
  uint64_t maxResourceUsage() const { return max_; };

protected:
  uint64_t max_;
  std::atomic<uint64_t> current_;
};

} // namespace Server
} // namespace Envoy
