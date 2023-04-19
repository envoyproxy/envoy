#pragma once

#include <functional>

#include "envoy/upstream/upstream.h"

#include "test/mocks/upstream/cluster_info.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
class MockCluster : public Cluster {
public:
  MockCluster();
  ~MockCluster() override;

  // Upstream::Cluster
  MOCK_METHOD(HealthChecker*, healthChecker, ());
  MOCK_METHOD(ClusterInfoConstSharedPtr, info, (), (const));
  MOCK_METHOD(Outlier::Detector*, outlierDetector, ());
  MOCK_METHOD(const Outlier::Detector*, outlierDetector, (), (const));
  MOCK_METHOD(void, initialize, (std::function<void()> callback));
  MOCK_METHOD(InitializePhase, initializePhase, (), (const));
  MOCK_METHOD(PrioritySet&, prioritySet, ());
  MOCK_METHOD(const PrioritySet&, prioritySet, (), (const));

  std::shared_ptr<MockClusterInfo> info_{new ::testing::NiceMock<MockClusterInfo>()};
  std::function<void()> initialize_callback_;
  Network::Address::InstanceConstSharedPtr source_address_;
};

class MockDfpCluster : public DfpCluster {
public:
  MockDfpCluster(){};
  ~MockDfpCluster() override = default;

  // Upstream::DFPCluster
  MOCK_METHOD(bool, enableSubCluster, (), (const));
  MOCK_METHOD((std::pair<bool, absl::optional<envoy::config::cluster::v3::Cluster>>),
              createSubClusterConfig, (const std::string&, const std::string&, const int));
  MOCK_METHOD(bool, touch, (const std::string&));
};
} // namespace Upstream
} // namespace Envoy
