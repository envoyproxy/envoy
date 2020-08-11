#pragma once

#include "envoy/upstream/load_balancer.h"

#include "test/mocks/upstream/host.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
class MockLoadBalancer : public LoadBalancer {
public:
  MockLoadBalancer();
  ~MockLoadBalancer() override;

  // Upstream::LoadBalancer
  MOCK_METHOD(HostConstSharedPtr, chooseHost, (LoadBalancerContext * context));

  std::shared_ptr<MockHost> host_{new MockHost()};
};
} // namespace Upstream
} // namespace Envoy
