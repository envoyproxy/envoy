#pragma once

#include "envoy/config/resource_monitor/downstream_connections/v3/downstream_connections.pb.h"
#include "envoy/server/resource_monitor.h"

#include "absl/container/node_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace DownstreamConnectionsMonitor {

/**
 * Downstream connections monitor with a statically configured maximum.
 */
class DownstreamConnectionsMonitor : public Server::ResourceMonitor {
public:
  DownstreamConnectionsMonitor(
      const envoy::config::resource_monitor::fixed_heap::v3::DownstreamConnectionsConfig& config);

  void updateResourceUsage(Server::ResourceMonitor::Callbacks& callbacks) override;

  bool updateResourceStats(const std::thread::id thread_id, const std::string& stat_name,
                           const uint64_t value) override;

private:
  std::atomic<uint64_t> global_total_downstream_conns_;
  const uint64_t max_downstream_conns_;
  absl::node_hash_map<std::string, uint64_t> thread_local_total_downstream_conns_;
};

} // namespace DownstreamConnectionsMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
