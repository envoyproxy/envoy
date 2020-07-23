#pragma once

#include "envoy/upstream/cluster_manager.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
class MockClusterUpdateCallbacksHandle : public ClusterUpdateCallbacksHandle {
public:
  MockClusterUpdateCallbacksHandle();
  ~MockClusterUpdateCallbacksHandle() override;
};
} // namespace Upstream
} // namespace Envoy
