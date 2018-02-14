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
  MOCK_CONST_METHOD0(connectTimeout, std::chrono::milliseconds());
  MOCK_CONST_METHOD0(perConnectionBufferLimitBytes, uint32_t());
  MOCK_CONST_METHOD0(features, uint64_t());
  MOCK_CONST_METHOD0(http2Settings, const Http::Http2Settings&());
  MOCK_CONST_METHOD0(lbConfig, const envoy::api::v2::Cluster::CommonLbConfig&());
  MOCK_CONST_METHOD0(lbType, LoadBalancerType());
  MOCK_CONST_METHOD0(type, envoy::api::v2::Cluster::DiscoveryType());
  MOCK_CONST_METHOD0(lbRingHashConfig,
                     const Optional<envoy::api::v2::Cluster::RingHashLbConfig>&());
  MOCK_CONST_METHOD0(maintenanceMode, bool());
  MOCK_CONST_METHOD0(maxRequestsPerConnection, uint64_t());
  MOCK_CONST_METHOD0(name, const std::string&());
  MOCK_CONST_METHOD1(resourceManager, ResourceManager&(ResourcePriority priority));
  MOCK_CONST_METHOD0(transportSocketFactory, Network::TransportSocketFactory&());
  MOCK_CONST_METHOD0(stats, ClusterStats&());
  MOCK_CONST_METHOD0(statsScope, Stats::Scope&());
  MOCK_CONST_METHOD0(loadReportStats, ClusterLoadReportStats&());
  MOCK_CONST_METHOD0(sourceAddress, const Network::Address::InstanceConstSharedPtr&());
  MOCK_CONST_METHOD0(lbSubsetInfo, const LoadBalancerSubsetInfo&());
  MOCK_CONST_METHOD0(metadata, const envoy::api::v2::core::Metadata&());

  std::string name_{"fake_cluster"};
  Http::Http2Settings http2_settings_{};
  uint64_t max_requests_per_connection_{};
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  ClusterStats stats_;
  Network::TransportSocketFactoryPtr transport_socket_factory_;
  NiceMock<Stats::MockIsolatedStatsStore> load_report_stats_store_;
  ClusterLoadReportStats load_report_stats_;
  NiceMock<Runtime::MockLoader> runtime_;
  std::unique_ptr<Upstream::ResourceManager> resource_manager_;
  Network::Address::InstanceConstSharedPtr source_address_;
  LoadBalancerType lb_type_{LoadBalancerType::RoundRobin};
  envoy::api::v2::Cluster::DiscoveryType type_{envoy::api::v2::Cluster::STRICT_DNS};
  NiceMock<MockLoadBalancerSubsetInfo> lb_subset_;
  Optional<envoy::api::v2::Cluster::RingHashLbConfig> lb_ring_hash_config_;
  envoy::api::v2::Cluster::CommonLbConfig lb_config_;
};

} // namespace Upstream
} // namespace Envoy
