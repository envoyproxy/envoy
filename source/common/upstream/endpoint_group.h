#pragma once

#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/upstream/upstream.h"

#include "common/upstream/endpoint_group_monitor.h"

namespace Envoy {
namespace Upstream {

class ActiveEndpointGroup {
public:
  ActiveEndpointGroup(EndpointGroupMonitorSharedPtr monitor) : monitors_({monitor}) {}
  ActiveEndpointGroup() {}

  bool update(const envoy::config::endpoint::v3::EndpointGroup& group,
              absl::string_view version_info);

  bool batchUpdate(const envoy::config::endpoint::v3::EndpointGroup& group,
                   absl::string_view version_info, bool all_endpoint_groups_updated);

  void addMonitor(EndpointGroupMonitorSharedPtr monitor) {
    if (!monitors_.count(monitor)) {
      monitors_.insert(monitor);
    }
  }
  void removeMonitor(EndpointGroupMonitorSharedPtr monitor) {
    ASSERT(monitors_.count(monitor));
    monitors_.erase(monitor);
  }

  bool findMonitor(EndpointGroupMonitorSharedPtr monitor) const { return monitors_.count(monitor); }

private:
  MonitorSet monitors_;
};

using ActiveEndpointGroupSharedPtr = std::shared_ptr<ActiveEndpointGroup>;

} // namespace Upstream
} // namespace Envoy
