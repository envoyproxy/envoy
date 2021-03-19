#pragma once

#include <memory>

#include "envoy/common/exception.h"
#include "envoy/common/pure.h"

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

class ResourceMonitor {
public:
  virtual ~ResourceMonitor() = default;

  /**
   * Notifies caller of updated resource usage.
   */
  class Callbacks {
  public:
    virtual ~Callbacks() = default;

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

  /**
   * Recalculate resource usage.
   * This must be non-blocking so if RPCs need to be made they should be
   * done asynchronously and invoke the callback when finished.
   */
  virtual void updateResourceUsage(Callbacks& callbacks) PURE;
};

class ReactiveResourceMonitor : public ResourceMonitor {
public:
  ReactiveResourceMonitor(uint64_t max, uint64_t current) : max_(max), current_(current){};
  virtual ~ReactiveResourceMonitor() = default;
  using ResourceMonitor::updateResourceUsage;
  virtual void updateResourceUsage(uint64_t curr_value, Callbacks& callbacks) PURE;
  uint64_t currentResourceUsage() const { return current_.load(); }
  uint64_t maxResourceUsage() const { return max_; };

protected:
  uint64_t max_;
  std::atomic<uint64_t> current_;
};

using ResourceMonitorPtr = std::unique_ptr<ResourceMonitor>;

using ReactiveResourceMonitorPtr = std::unique_ptr<ReactiveResourceMonitor>;

// Example of reactive resource monitor. To be removed.
class ActiveConnectionsResourceMonitor : public ReactiveResourceMonitor {
public:
  ActiveConnectionsResourceMonitor(uint64_t max_active_conns)
      : ReactiveResourceMonitor(max_active_conns, 0){};

  void updateResourceUsage(uint64_t increment, Callbacks& callbacks) {
    // Calling code will need to reset its current value after this.
    current_ += increment;
    // Invoke callback actions.

    Server::ResourceUsage usage;
    usage.resource_pressure_ = currentResourceUsage() / maxResourceUsage();

    callbacks.onSuccess(usage);
  }

  void updateResourceUsage(Callbacks&) { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
};

} // namespace Server
} // namespace Envoy
