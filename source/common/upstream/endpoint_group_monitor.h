#pragma once

#include <memory>
#include <string>

#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/upstream/upstream.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Upstream {

class EndpointGroupMonitor {
public:
  virtual ~EndpointGroupMonitor() = default;

  virtual void update(const envoy::config::endpoint::v3::EndpointGroup& group,
                      absl::string_view version_info) PURE;
};

using EndpointGroupMonitorSharedPtr = std::shared_ptr<EndpointGroupMonitor>;
using MonitorSet = absl::flat_hash_set<EndpointGroupMonitorSharedPtr>;

class EndpointGroupMonitorManager {
public:
  virtual ~EndpointGroupMonitorManager() = default;
  virtual void addMonitor(EndpointGroupMonitorSharedPtr monitor, absl::string_view group_name) PURE;
  virtual void removeMonitor(EndpointGroupMonitorSharedPtr monitor,
                             absl::string_view group_name) PURE;
};

} // namespace Upstream
} // namespace Envoy
