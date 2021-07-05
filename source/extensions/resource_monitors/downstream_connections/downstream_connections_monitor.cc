#include "source/extensions/resource_monitors/downstream_connections/downstream_connections_monitor.h"

#include "envoy/extensions/resource_monitors/downstream_connections/v3/downstream_connections.pb.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace DownstreamConnections {

ActiveDownstreamConnectionsResourceMonitor::ActiveDownstreamConnectionsResourceMonitor(
    const envoy::extensions::resource_monitors::downstream_connections::v3::
        DownstreamConnectionsConfig& config)
    : max_(config.max_active_downstream_connections()), current_(0){};

bool ActiveDownstreamConnectionsResourceMonitor::tryAllocateResource(int64_t increment) {
  int64_t new_val = (current_ += increment);
  if (new_val > static_cast<int64_t>(max_) || new_val < 0) {
    current_ -= increment;
    return false;
  }
  return true;
}

bool ActiveDownstreamConnectionsResourceMonitor::tryDeallocateResource(int64_t decrement) {
  RELEASE_ASSERT(decrement <= current_,
                 "Cannot deallocate resource, current resource usage is lower than decrement");
  int64_t new_val = (current_ -= decrement);
  if (new_val < 0) {
    current_ += decrement;
    return false;
  }
  return true;
}

int64_t ActiveDownstreamConnectionsResourceMonitor::currentResourceUsage() const {
  return current_.load();
}
uint64_t ActiveDownstreamConnectionsResourceMonitor::maxResourceUsage() const { return max_; };

} // namespace DownstreamConnections
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
