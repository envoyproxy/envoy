#pragma once

#include "envoy/upstream/cluster_manager.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {

class MockClusterDiscoveryCallbackHandle : public ClusterDiscoveryCallbackHandle {
  MockClusterDiscoveryCallbackHandle();
  ~MockClusterDiscoveryCallbackHandle() override;
};

} // namespace Upstream
} // namespace Envoy
