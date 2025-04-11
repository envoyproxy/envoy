#pragma once

#include "envoy/config/typed_config.h"

namespace Envoy {
namespace Upstream {

class LoadBalancerContext;

struct OverrideHost {
  // The host address to be selected directly. If multiple hosts are provided, the addresses
  // will be tried in order. Multiple addresses are separated by comma.
  std::string hosts;

  // If the strict is set to true and no host in the hosts list is valid, then the upstream
  // will return an error.
  // Or if the strict is set to false and no host in the hosts list is valid, then the upstream
  // will continue to select the host by the load balancing algorithm.
  bool strict{};
};

class OverrideHostPolicy {
public:
  virtual ~OverrideHostPolicy() = default;

  /**
   * Returns the host that should be selected directly. If the expected host exists and
   * be considered as valid, the load balancing will be bypassed and the host will be
   * selected directly.
   *
   * @param ctx supplies the context which is used in host selection.
   * @return the pair of host address and whether the result should be obeyed strictly.
   *         If the second element is true, the load balancer should select the host
   */
  virtual absl::optional<OverrideHost> overrideHostToSelect(LoadBalancerContext* ctx) const PURE;
};

using OverrideHostPolicySharedPtr = std::shared_ptr<OverrideHostPolicy>;

class OverrideHostPolicyFactory : public Config::TypedFactory {
public:
  virtual OverrideHostPolicySharedPtr overrideHostPolicy(const Protobuf::Message&) PURE;

  std::string category() const override { return "envoy.override_host_policies"; }
};

} // namespace Upstream
} // namespace Envoy
