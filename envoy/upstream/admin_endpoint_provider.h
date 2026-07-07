#pragma once

#include <string>
#include <utility>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/network/address.h"

namespace Envoy {
namespace Upstream {

/**
 * Optional cluster capability for reporting display-only endpoints on the admin /clusters endpoint.
 * These are not load-balanced hosts: they never enter the PrioritySet or affect routing. Lets a
 * cluster surface a reachable set it knows independently of traffic (e.g. reverse-tunnel nodes).
 */
class AdminEndpointProvider {
public:
  virtual ~AdminEndpointProvider() = default;

  struct AdminEndpoint {
    // Rendered as the host's address.
    Network::Address::InstanceConstSharedPtr address;
    // Rendered as the host's hostname.
    std::string hostname;
    // Rendered as the host's weight.
    uint32_t weight{1};
    // Rendered as the host's eds_health_status.
    envoy::config::core::v3::HealthStatus health{envoy::config::core::v3::HEALTHY};
    // Extra gauges to surface (e.g. connection counts).
    std::vector<std::pair<std::string, uint64_t>> gauges;
  };

  /**
   * @return display-only endpoints to render on /clusters. Produced on demand; eventually
   *         consistent.
   */
  virtual std::vector<AdminEndpoint> adminEndpoints() const PURE;
};

} // namespace Upstream
} // namespace Envoy
