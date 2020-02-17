#include "common/upstream/endpoint_group.h"

namespace Envoy {
namespace Upstream {

bool ActiveEndpointGroup::update(const envoy::config::endpoint::v3::EndpointGroup& group,
                                 absl::string_view version_info) {
  for (auto& monitor : monitors_) {
    monitor->update(group, version_info);
  }

  return true;
}

bool ActiveEndpointGroup::batchUpdate(const envoy::config::endpoint::v3::EndpointGroup& group,
                                      absl::string_view version_info,
                                      bool all_endpoint_groups_updated) {
  for (auto& monitor : monitors_) {
    monitor->batchUpdate(group, version_info, all_endpoint_groups_updated);
  }

  return true;
}

} // namespace Upstream
} // namespace Envoy