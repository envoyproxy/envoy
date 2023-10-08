#pragma once

#include <string>

#include "envoy/upstream/cluster_manager.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
class MockClusterUpdateCallbacks : public ClusterUpdateCallbacks {
public:
  MockClusterUpdateCallbacks();
  ~MockClusterUpdateCallbacks() override;

  MOCK_METHOD(void, onClusterAddOrUpdate,
              (absl::string_view cluster_name, ThreadLocalClusterCommand& command));
  MOCK_METHOD(void, onClusterRemoval, (const std::string& cluster_name));
};
} // namespace Upstream
} // namespace Envoy
