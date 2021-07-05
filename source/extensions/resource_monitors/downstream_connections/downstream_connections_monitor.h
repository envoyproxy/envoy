#pragma once

#include "envoy/extensions/resource_monitors/downstream_connections/v3/downstream_connections.pb.h"
#include "envoy/server/overload/proactive_resource_monitor.h"

#include "source/common/common/assert.h"

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
  uint64_t maxResourceUsage() const override;

protected:
  uint64_t max_;
  std::atomic<int64_t> current_;
};

} // namespace DownstreamConnections
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy