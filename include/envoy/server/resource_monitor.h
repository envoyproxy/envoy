#pragma once

#include <memory>

#include "envoy/common/exception.h"
#include "envoy/common/pure.h"

#include "common/singleton/const_singleton.h"

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

  /**
   * Update specific stats within resource monitor.
   * Currently only integer values are supported.
   */
  virtual bool updateResourceStats(const std::thread::id thread_id, const std::string& stat_name,
                                   const uint64_t value) PURE;
};

using ResourceMonitorPtr = std::unique_ptr<ResourceMonitor>;

/**
 * Well-known resource stats names.
 */
class ResourceStatsNameValues {
public:
  // todo(nezdolik) add description
  const std::string DownstreamConnsTotal = "downstream_cx_total";

  // todo(nezdolik) add description
  const std::string DownstreamRequestsTotal = "downstream_rq_total";

  // todo(nezdolik) add description
  const std::string UpstreamConnsTotal = "upstream_cx_total";
};

using ResourceStatsNames = ConstSingleton<ResourceStatsNameValues>;

} // namespace Server
} // namespace Envoy
