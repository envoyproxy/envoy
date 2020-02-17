#pragma once

#include <memory>

#include "envoy/config/endpoint/v3/endpoint.pb.h"

#include "common/upstream/endpoint_group.h"
#include "common/upstream/endpoint_group_monitor.h"
#include "common/upstream/endpoint_groups_manager.h"

namespace Envoy {
namespace Upstream {

class EndpointGroupsManagerImpl : public EndpointGroupsManager, public EndpointGroupMonitorManager {
public:
  ~EndpointGroupsManagerImpl() override = default;

  // Upstream::EndpointGroupsManager
  bool addOrUpdateEndpointGroup(const envoy::config::endpoint::v3::EndpointGroup& group,
                                absl::string_view version_info) override;
  bool removeEndpointGroup(absl::string_view name) override;
  bool clearEndpointGroup(absl::string_view name, absl::string_view version_info) override;
  bool
  batchUpdateEndpointGroup(const std::vector<envoy::config::endpoint::v3::EndpointGroup>& added,
                           const std::vector<std::string> removed,
                           absl::string_view version_info) override;

  // Upstream::EndpointGroupMonitorManager
  void addMonitor(EndpointGroupMonitorSharedPtr monitor, absl::string_view group_name) override;
  void removeMonitor(EndpointGroupMonitorSharedPtr monitor, absl::string_view group_name) override;

  bool findMonitor(EndpointGroupMonitorSharedPtr monitor, absl::string_view group_name) const;

private:
  absl::flat_hash_map<std::string /*group name*/, ActiveEndpointGroupSharedPtr> active_groups_;
};

} // namespace Upstream
} // namespace Envoy
