#pragma once

#include "cluster.h"

namespace Envoy {
namespace Upstream {
class MockClusterRealPrioritySet : public MockCluster {
public:
  MockClusterRealPrioritySet();
  ~MockClusterRealPrioritySet() override;

  // Upstream::Cluster
  PrioritySetImpl& prioritySet() override { return priority_set_; }
  const PrioritySet& prioritySet() const override { return priority_set_; }

  PrioritySetImpl priority_set_;
};
} // namespace Upstream
} // namespace Envoy
