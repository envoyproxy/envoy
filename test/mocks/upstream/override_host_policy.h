#pragma once

#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/override_host_policy.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Upstream {

class MockOverrideHostPolicy : public OverrideHostPolicy {
public:
  MockOverrideHostPolicy();

  MOCK_METHOD(absl::optional<OverrideHost>, overrideHostToSelect, (LoadBalancerContext*), (const));

  absl::optional<OverrideHost> override_host_;
};

} // namespace Upstream
} // namespace Envoy
