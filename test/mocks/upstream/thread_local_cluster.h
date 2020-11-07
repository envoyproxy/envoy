#pragma once

#include "envoy/upstream/thread_local_cluster.h"

#include "cluster_priority_set.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "load_balancer.h"

namespace Envoy {
namespace Upstream {
using ::testing::NiceMock;
class MockThreadLocalCluster : public ThreadLocalCluster {
public:
  MockThreadLocalCluster();
  ~MockThreadLocalCluster() override;

  // Upstream::ThreadLocalCluster
  MOCK_METHOD(const PrioritySet&, prioritySet, ());
  MOCK_METHOD(ClusterInfoConstSharedPtr, info, ());
  MOCK_METHOD(LoadBalancer&, loadBalancer, ());

  NiceMock<MockClusterMockPrioritySet> cluster_;
  NiceMock<MockLoadBalancer> lb_;
};
} // namespace Upstream
} // namespace Envoy
