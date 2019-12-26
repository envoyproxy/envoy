#pragma once

#include <string>

#include "envoy/config/endpoint/v3/endpoint.pb.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Upstream {

class EndpointGroupsManager {
public:
  virtual ~EndpointGroupsManager() = default;

  virtual bool addOrUpdateEndpointGroup(const envoy::config::endpoint::v3::EndpointGroup& group,
                                        absl::string_view version_info) PURE;
  virtual bool removeEndpointGroup(absl::string_view name) PURE;
  virtual bool clearEndpointGroup(absl::string_view name, absl::string_view version_info) PURE;
};

using EndpointGroupsManagerPtr = std::unique_ptr<EndpointGroupsManager>;

} // namespace Upstream
} // namespace Envoy
