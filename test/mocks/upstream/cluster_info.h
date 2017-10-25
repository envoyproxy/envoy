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

using testing::NiceMock;

namespace Envoy {
namespace Upstream {

class MockCircuitBreakerConfig : public ClusterInfo::CircuitBreakerConfig {
public:
  MockCircuitBreakerConfig();
  ~MockCircuitBreakerConfig();

  // Upstream::ClusterInfo::CircuitBreakerConfig
  MOCK_CONST_METHOD0(thresholds, const std::vector<Thresholds::ConstSharedPtr>&());
};

class MockOutlierDetectionConfig : public ClusterInfo::OutlierDetectionConfig {
public:
  MockOutlierDetectionConfig();
  ~MockOutlierDetectionConfig();

  // Upstream::ClusterInfo::OutlierDetectionConfig
  MOCK_CONST_METHOD0(intervalMs, uint64_t());
  MOCK_CONST_METHOD0(baseEjectionTimeMs, uint64_t());
  MOCK_CONST_METHOD0(consecutive5xx, uint64_t());
  MOCK_CONST_METHOD0(maxEjectionPercent, uint64_t());
  MOCK_CONST_METHOD0(successRateMinimumHosts, uint64_t());
  MOCK_CONST_METHOD0(successRateRequestVolume, uint64_t());
  MOCK_CONST_METHOD0(successRateStdevFactor, uint64_t());
  MOCK_CONST_METHOD0(enforcingConsecutive5xx, uint64_t());
  MOCK_CONST_METHOD0(enforcingSuccessRate, uint64_t());
};

class MockLoadBalancerSubsetInfo : public LoadBalancerSubsetInfo {
public:
  MockLoadBalancerSubsetInfo();
  ~MockLoadBalancerSubsetInfo();

  // Upstream::LoadBalancerSubsetInfo
  MOCK_CONST_METHOD0(isEnabled, bool());
  MOCK_CONST_METHOD0(fallbackPolicy,
                     envoy::api::v2::Cluster::LbSubsetConfig::LbSubsetFallbackPolicy());
  MOCK_CONST_METHOD0(defaultSubset, const ProtobufWkt::Struct&());
  MOCK_CONST_METHOD0(subsetKeys, const std::vector<std::set<std::string>>&());

  std::vector<std::set<std::string>> subset_keys_;
};

class MockClusterInfo : public ClusterInfo {
public:
  MockClusterInfo();
  ~MockClusterInfo();

  // Upstream::ClusterInfo
  MOCK_CONST_METHOD0(addedViaApi, bool());
  MOCK_CONST_METHOD0(connectTimeout, const std::chrono::milliseconds&());
  MOCK_CONST_METHOD0(perConnectionBufferLimitBytes, uint32_t());
  MOCK_CONST_METHOD0(features, uint64_t());
  MOCK_CONST_METHOD0(http2Settings, const Http::Http2Settings&());
  MOCK_CONST_METHOD0(lbType, LoadBalancerType());
  MOCK_CONST_METHOD0(maintenanceMode, bool());
  MOCK_CONST_METHOD0(maxRequestsPerConnection, uint64_t());
  MOCK_CONST_METHOD0(name, const std::string&());
  MOCK_CONST_METHOD1(resourceManager, ResourceManager&(ResourcePriority priority));
  MOCK_CONST_METHOD0(sslContext, Ssl::ClientContext*());
  MOCK_CONST_METHOD0(tlsContextConfig, const Ssl::ClientContextConfig::ConstSharedPtr&());
  MOCK_CONST_METHOD0(stats, ClusterStats&());
  MOCK_CONST_METHOD0(statsScope, Stats::Scope&());
  MOCK_CONST_METHOD0(loadReportStats, ClusterLoadReportStats&());
  MOCK_CONST_METHOD0(sourceAddress, const Network::Address::InstanceConstSharedPtr&());
  MOCK_CONST_METHOD0(lbSubsetInfo, const LoadBalancerSubsetInfo&());
  MOCK_CONST_METHOD0(outlierDetectionConfig, const OutlierDetectionConfig::ConstSharedPtr&());
  MOCK_CONST_METHOD0(circuitBreakerConfig, const CircuitBreakerConfig&());
  MOCK_METHOD1_T(overrideWithHeaders, void(const Http::HeaderMap& headers));

  std::string name_{"fake_cluster"};
  std::chrono::milliseconds connection_timeout_;
  Http::Http2Settings http2_settings_{};
  uint64_t max_requests_per_connection_{};
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  ClusterStats stats_;
  NiceMock<Stats::MockIsolatedStatsStore> load_report_stats_store_;
  ClusterLoadReportStats load_report_stats_;
  NiceMock<Runtime::MockLoader> runtime_;
  std::unique_ptr<Upstream::ResourceManager> resource_manager_;
  Network::Address::InstanceConstSharedPtr source_address_;
  LoadBalancerType lb_type_{LoadBalancerType::RoundRobin};
  NiceMock<MockLoadBalancerSubsetInfo> lb_subset_;
  OutlierDetectionConfig::ConstSharedPtr outlier_detection_config_;
  NiceMock<MockCircuitBreakerConfig> circuit_breaker_config_;
};

} // namespace Upstream
} // namespace Envoy
