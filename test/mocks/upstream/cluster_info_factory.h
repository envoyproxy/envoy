#pragma once

#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
class MockClusterInfoFactory : public ClusterInfoFactory, Logger::Loggable<Logger::Id::upstream> {
public:
  MockClusterInfoFactory();
  ~MockClusterInfoFactory() override;

  MOCK_METHOD(ClusterInfoConstSharedPtr, createClusterInfo, (const CreateClusterInfoParams&));
};
} // namespace Upstream
} // namespace Envoy
