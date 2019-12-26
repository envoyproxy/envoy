#include "common/upstream/endpoint_groups_manager_impl.h"

namespace Envoy {
namespace Upstream {

bool EndpointGroupsManagerImpl::addOrUpdateEndpointGroup(
    const envoy::config::endpoint::v3::EndpointGroup& group, absl::string_view version_info) {
  if (active_groups_.find(group.name()) == active_groups_.end()) {
    active_groups_.emplace(group.name(), std::make_shared<ActiveEndpointGroup>());
  }

  return active_groups_[group.name()]->update(group, version_info);
}

bool EndpointGroupsManagerImpl::clearEndpointGroup(absl::string_view name,
                                                   absl::string_view version_info) {
  ASSERT(active_groups_.find(name) != active_groups_.end());

  // Clear all endpoints with an empty EndpointGroup.
  envoy::config::endpoint::v3::EndpointGroup empty_group;
  empty_group.set_name(name.data());
  return active_groups_[name]->update(empty_group, version_info);
}

bool EndpointGroupsManagerImpl::removeEndpointGroup(absl::string_view name) {
  ASSERT(active_groups_.find(name) != active_groups_.end());
  return active_groups_.erase(name);
}

void EndpointGroupsManagerImpl::addMonitor(EndpointGroupMonitorSharedPtr monitor,
                                           absl::string_view group_name) {
  if (active_groups_.find(group_name) == active_groups_.end()) {
    active_groups_.emplace(group_name, std::make_shared<ActiveEndpointGroup>(monitor));
    return;
  }

  active_groups_[group_name]->addMonitor(monitor);
}

void EndpointGroupsManagerImpl::removeMonitor(EndpointGroupMonitorSharedPtr monitor,
                                              absl::string_view group_name) {
  if (active_groups_.find(group_name) != active_groups_.end()) {
    active_groups_[group_name]->removeMonitor(monitor);
  }
}

bool EndpointGroupsManagerImpl::findMonitor(EndpointGroupMonitorSharedPtr monitor,
                                            absl::string_view group_name) const {
  return active_groups_.count(group_name) &&
         active_groups_.at(group_name.data())->findMonitor(monitor);
}

} // namespace Upstream
} // namespace Envoy
