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

} // namespace Upstream
} // namespace Envoy