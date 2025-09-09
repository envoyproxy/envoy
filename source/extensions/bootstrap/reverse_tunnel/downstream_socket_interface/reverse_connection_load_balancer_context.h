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
      : host_string_(host_address), host_to_select_(host_string_, false) {}

  // Upstream::LoadBalancerContext overrides
  absl::optional<OverrideHost> overrideHostToSelect() const override {
    return absl::make_optional(host_to_select_);
  }

private:
  // Own the string data. This is to prevent use after free when the host_to_select
  // is destroyed.
  std::string host_string_;
  OverrideHost host_to_select_;
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
