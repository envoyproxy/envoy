#pragma once

#include <string>

#include "envoy/upstream/load_balancer.h"

#include "source/common/upstream/load_balancer_context_base.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

/**
 * Load balancer context for reverse connections.
 * This context is used to select specific upstream hosts by address.
 */
class ReverseConnectionLoadBalancerContext : public Upstream::LoadBalancerContextBase {
public:
  /**
   * Constructor that sets the host to select.
   * @param host_address the address of the host to select
   */
  explicit ReverseConnectionLoadBalancerContext(const std::string& host_address)
      : host_to_select_{host_address, false} {}

  // Upstream::LoadBalancerContext overrides
  OptRef<const OverrideHost> overrideHostToSelect() const override { return host_to_select_; }

private:
  OverrideHost host_to_select_;
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
