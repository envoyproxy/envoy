#pragma once

#include "envoy/extensions/resource_monitors/downstream_connections/v3/downstream_connections.pb.h"
#include "envoy/server/proactive_resource_monitor.h"

#include "source/common/common/thread_synchronizer.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace DownstreamConnections {

class ActiveDownstreamConnectionsResourceMonitor : public Server::ProactiveResourceMonitor {
public:
  ActiveDownstreamConnectionsResourceMonitor(
      const envoy::extensions::resource_monitors::downstream_connections::v3::
          DownstreamConnectionsConfig& config);

  bool tryAllocateResource(int64_t increment) override;

  bool tryDeallocateResource(int64_t decrement) override;

  int64_t currentResourceUsage() const override;
  int64_t maxResourceUsage() const override;

protected:
  const int64_t max_;
  std::atomic<int64_t> current_;
  // Used for testing only.
  mutable Thread::ThreadSynchronizer synchronizer_;

  friend class ActiveDownstreamConnectionsMonitorTest;
};

} // namespace DownstreamConnections
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
