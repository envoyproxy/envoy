#pragma once

#include "extensions/resource_monitors/downstream_connections/downstream_connections_monitor.h"

#include <thread>

#include "envoy/config/resource_monitor/downstream_connections/v3/downstream_connections.pb.h"

#include "common/common/assert.h"

// tbd

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace DownstreamConnectionsMonitor {

DownstreamConnectionsMonitor::DownstreamConnectionsMonitor(
    const envoy::config::resource_monitor::fixed_heap::v3::DownstreamConnectionsConfig& config)
    : max_downstream_conns_(config.max_downstream_connections()),
      global_total_downstream_conns_(0) {
  ASSERT(max_downstream_conns_ > 0);
}

void DownstreamConnectionsMonitor::updateResourceUsage(
    Server::ResourceMonitor::Callbacks& callbacks) {
  Server::ResourceUsage usage;
  global_total_downstream_conns_.exchange(0);
  for (auto& downstream_conns_ : thread_local_total_downstream_conns_) {
    global_total_downstream_conns_.fetch_add(downstream_conns_.second);
  }
  usage.resource_pressure_ = static_cast<double>(global_total_downstream_conns_.load()) /
                             static_cast<double>(max_downstream_conns_);
  callbacks.onSuccess(usage);
}

bool updateResourceStats(const std::thread::id& thread_id, const std::string& stat_name,
                         const uint64_t value) {
  if (stat_name == Server::ResourceStatsNames::get().DownstreamConnsTotal) {
    thread_local_total_downstream_conns_.insert_or_assign(thread_id, value);
  } else {
    ENVOY_LOG(debug, "Cannot update unknown stats {} in downstream connections monitor", stat_name);
    return false;
  }
}

} // namespace DownstreamConnectionsMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
