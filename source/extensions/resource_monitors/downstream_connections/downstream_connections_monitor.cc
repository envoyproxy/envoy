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
  // No synchronization is imposed on other reads or writes.
  auto current = current_.load(std::memory_order_relaxed);
  while (current + increment <= max_) {
    // Testing hook.
    synchronizer_.syncPoint("try_allocate_pre_cas");
    // `current_` value is atomically compared to `current`.
    // In case values are bitwise equal, `current` value is replaced with `current + increment`.
    // The write will be visible to other threads accessing `current_`.
    // If `current` is not equal to value stored in `current_`, `current` will be reloaded with the
    // latest value of `current_` and no synchronization will be imposed on other reads or writes.
    // After value reload next loop iteration will be attempted until suggested increment breaches
    // `max_` or cas operation is successful.
    if (current_.compare_exchange_weak(current, current + increment, std::memory_order_release,
                                       std::memory_order_relaxed)) {
      return true;
    }
  }
  return false;
}

bool ActiveDownstreamConnectionsResourceMonitor::tryDeallocateResource(int64_t decrement) {
  // No synchronization is imposed on other reads or writes.
  auto current = current_.load(std::memory_order_relaxed);
  while (current - decrement >= 0) {
    // Testing hook.
    synchronizer_.syncPoint("try_deallocate_pre_cas");
    // `current_` value is atomically compared to `current`.
    // In case values are bitwise equal, `current` value is replaced with `current - increment`.
    // The write will be visible to other threads accessing `current_`.
    // If `current` is not equal to value stored in `current_`, `current` will be reloaded with the
    // latest value of `current_` and no synchronization will be imposed on other reads or writes.
    // After value reload next loop iteration will be attempted until suggested decrement goes below
    // 0 or cas operation is successful.
    if (current_.compare_exchange_weak(current, current - decrement, std::memory_order_release,
                                       std::memory_order_relaxed)) {
      return true;
    }
  }
  return false;
}

int64_t ActiveDownstreamConnectionsResourceMonitor::currentResourceUsage() const {
  return current_.load();
}
int64_t ActiveDownstreamConnectionsResourceMonitor::maxResourceUsage() const { return max_; };

} // namespace DownstreamConnections
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
