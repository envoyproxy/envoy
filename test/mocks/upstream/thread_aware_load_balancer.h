#pragma once

#include "envoy/upstream/load_balancer.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
class MockThreadAwareLoadBalancer : public ThreadAwareLoadBalancer {
public:
  MockThreadAwareLoadBalancer();
  ~MockThreadAwareLoadBalancer() override;

  // Upstream::ThreadAwareLoadBalancer
  MOCK_METHOD(LoadBalancerFactorySharedPtr, factory, ());
  MOCK_METHOD(void, initialize, ());
};
} // namespace Upstream
} // namespace Envoy
