#pragma once

#include "cluster.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "priority_set.h"

namespace Envoy {
namespace Upstream {
class MockClusterMockPrioritySet : public MockCluster {
public:
  MockClusterMockPrioritySet();
  ~MockClusterMockPrioritySet() override;

  // Upstream::Cluster
  MockPrioritySet& prioritySet() override { return priority_set_; }
  const PrioritySet& prioritySet() const override { return priority_set_; }

  ::testing::NiceMock<MockPrioritySet> priority_set_;
};
} // namespace Upstream
} // namespace Envoy
