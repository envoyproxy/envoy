#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
using testing::NiceMock;

namespace Upstream {

class MockClusterInfo : public ClusterInfo {
public:
  MockClusterInfo();
  ~MockClusterInfo();

  // Upstream::ClusterInfo
  MOCK_CONST_METHOD0(connectTimeout, std::chrono::milliseconds());
  MOCK_CONST_METHOD0(perConnectionBufferLimitBytes, uint32_t());
  MOCK_CONST_METHOD0(features, uint64_t());
  MOCK_CONST_METHOD0(http2Settings, const Http::Http2Settings&());
  MOCK_CONST_METHOD0(lbType, LoadBalancerType());
  MOCK_CONST_METHOD0(maintenanceMode, bool());
  MOCK_CONST_METHOD0(maxRequestsPerConnection, uint64_t());
  MOCK_CONST_METHOD0(name, const std::string&());
  MOCK_CONST_METHOD1(resourceManager, ResourceManager&(ResourcePriority priority));
  MOCK_CONST_METHOD0(sslContext, Ssl::ClientContext*());
  MOCK_CONST_METHOD0(stats, ClusterStats&());
  MOCK_CONST_METHOD0(statsScope, Stats::Scope&());

  std::string name_{"fake_cluster"};
  uint64_t max_requests_per_connection_{};
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  ClusterStats stats_;
  NiceMock<Runtime::MockLoader> runtime_;
  std::unique_ptr<Upstream::ResourceManager> resource_manager_;
  LoadBalancerType lb_type_{LoadBalancerType::RoundRobin};
};

} // Upstream
} // Envoy
