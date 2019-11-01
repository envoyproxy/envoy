#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/config/typed_metadata.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "common/upstream/upstream_impl.h"

#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/transport_socket_match.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Upstream {

class MockLoadBalancerSubsetInfo : public LoadBalancerSubsetInfo {
public:
  MockLoadBalancerSubsetInfo();
  ~MockLoadBalancerSubsetInfo() override;

  // Upstream::LoadBalancerSubsetInfo
  MOCK_CONST_METHOD0(isEnabled, bool());
  MOCK_CONST_METHOD0(fallbackPolicy,
                     envoy::api::v2::Cluster::LbSubsetConfig::LbSubsetFallbackPolicy());
  MOCK_CONST_METHOD0(defaultSubset, const ProtobufWkt::Struct&());
  MOCK_CONST_METHOD0(subsetSelectors, const std::vector<SubsetSelectorPtr>&());
  MOCK_CONST_METHOD0(localityWeightAware, bool());
  MOCK_CONST_METHOD0(scaleLocalityWeight, bool());
  MOCK_CONST_METHOD0(panicModeAny, bool());
  MOCK_CONST_METHOD0(listAsAny, bool());

  std::vector<SubsetSelectorPtr> subset_selectors_;
};

// While this mock class doesn't have any direct use in public Envoy tests, it's
// useful for constructing tests of downstream private filters that use
// ClusterTypedMetadata.
class MockClusterTypedMetadata : public Config::TypedMetadataImpl<ClusterTypedMetadataFactory> {
public:
  using Config::TypedMetadataImpl<ClusterTypedMetadataFactory>::TypedMetadataImpl;

  void inject(const std::string& key, std::unique_ptr<const TypedMetadata::Object> value) {
    data_[key] = std::move(value);
  }

  std::unordered_map<std::string, std::unique_ptr<const TypedMetadata::Object>>& data() {
    return data_;
  }
};

class MockClusterInfo : public ClusterInfo {
public:
  MockClusterInfo();
  ~MockClusterInfo() override;

  void resetResourceManager(uint64_t cx, uint64_t rq_pending, uint64_t rq, uint64_t rq_retry,
                            uint64_t conn_pool) {
    resource_manager_ = std::make_unique<ResourceManagerImpl>(
        runtime_, name_, cx, rq_pending, rq, rq_retry, conn_pool, circuit_breakers_stats_);
  }

  // Upstream::ClusterInfo
  MOCK_CONST_METHOD0(addedViaApi, bool());
  MOCK_CONST_METHOD0(connectTimeout, std::chrono::milliseconds());
  MOCK_CONST_METHOD0(idleTimeout, const absl::optional<std::chrono::milliseconds>());
  MOCK_CONST_METHOD0(perConnectionBufferLimitBytes, uint32_t());
  MOCK_CONST_METHOD0(features, uint64_t());
  MOCK_CONST_METHOD0(http1Settings, const Http::Http1Settings&());
  MOCK_CONST_METHOD0(http2Settings, const Http::Http2Settings&());
  MOCK_CONST_METHOD1(extensionProtocolOptions,
                     ProtocolOptionsConfigConstSharedPtr(const std::string&));
  MOCK_CONST_METHOD0(lbConfig, const envoy::api::v2::Cluster::CommonLbConfig&());
  MOCK_CONST_METHOD0(lbType, LoadBalancerType());
  MOCK_CONST_METHOD0(type, envoy::api::v2::Cluster::DiscoveryType());
  MOCK_CONST_METHOD0(clusterType,
                     const absl::optional<envoy::api::v2::Cluster::CustomClusterType>&());
  MOCK_CONST_METHOD0(lbRingHashConfig,
                     const absl::optional<envoy::api::v2::Cluster::RingHashLbConfig>&());
  MOCK_CONST_METHOD0(lbLeastRequestConfig,
                     const absl::optional<envoy::api::v2::Cluster::LeastRequestLbConfig>&());
  MOCK_CONST_METHOD0(lbOriginalDstConfig,
                     const absl::optional<envoy::api::v2::Cluster::OriginalDstLbConfig>&());
  MOCK_CONST_METHOD0(maintenanceMode, bool());
  MOCK_CONST_METHOD0(maxResponseHeadersCount, uint32_t());
  MOCK_CONST_METHOD0(maxRequestsPerConnection, uint64_t());
  MOCK_CONST_METHOD0(name, const std::string&());
  MOCK_CONST_METHOD1(resourceManager, ResourceManager&(ResourcePriority priority));
  MOCK_CONST_METHOD0(transportSocketMatcher, TransportSocketMatcher&());
  MOCK_CONST_METHOD0(stats, ClusterStats&());
  MOCK_CONST_METHOD0(statsScope, Stats::Scope&());
  MOCK_CONST_METHOD0(loadReportStats, ClusterLoadReportStats&());
  MOCK_CONST_METHOD0(sourceAddress, const Network::Address::InstanceConstSharedPtr&());
  MOCK_CONST_METHOD0(lbSubsetInfo, const LoadBalancerSubsetInfo&());
  MOCK_CONST_METHOD0(metadata, const envoy::api::v2::core::Metadata&());
  MOCK_CONST_METHOD0(typedMetadata, const Envoy::Config::TypedMetadata&());
  MOCK_CONST_METHOD0(clusterSocketOptions, const Network::ConnectionSocket::OptionsSharedPtr&());
  MOCK_CONST_METHOD0(drainConnectionsOnHostRemoval, bool());
  MOCK_CONST_METHOD0(warmHosts, bool());
  MOCK_CONST_METHOD0(eds_service_name, absl::optional<std::string>());
  MOCK_CONST_METHOD1(createNetworkFilterChain, void(Network::Connection&));
  MOCK_CONST_METHOD1(upstreamHttpProtocol, Http::Protocol(absl::optional<Http::Protocol>));

  std::string name_{"fake_cluster"};
  absl::optional<std::string> eds_service_name_;
  Http::Http1Settings http1_settings_;
  Http::Http2Settings http2_settings_;
  ProtocolOptionsConfigConstSharedPtr extension_protocol_options_;
  uint64_t max_requests_per_connection_{};
  uint32_t max_response_headers_count_{Http::DEFAULT_MAX_HEADERS_COUNT};
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  ClusterStats stats_;
  Upstream::TransportSocketMatcherPtr transport_socket_matcher_;
  NiceMock<Stats::MockIsolatedStatsStore> load_report_stats_store_;
  ClusterLoadReportStats load_report_stats_;
  ClusterCircuitBreakersStats circuit_breakers_stats_;
  NiceMock<Runtime::MockLoader> runtime_;
  std::unique_ptr<Upstream::ResourceManager> resource_manager_;
  Network::Address::InstanceConstSharedPtr source_address_;
  LoadBalancerType lb_type_{LoadBalancerType::RoundRobin};
  envoy::api::v2::Cluster::DiscoveryType type_{envoy::api::v2::Cluster::STRICT_DNS};
  absl::optional<envoy::api::v2::Cluster::CustomClusterType> cluster_type_;
  NiceMock<MockLoadBalancerSubsetInfo> lb_subset_;
  absl::optional<envoy::api::v2::Cluster::RingHashLbConfig> lb_ring_hash_config_;
  absl::optional<envoy::api::v2::Cluster::OriginalDstLbConfig> lb_original_dst_config_;
  Network::ConnectionSocket::OptionsSharedPtr cluster_socket_options_;
  envoy::api::v2::Cluster::CommonLbConfig lb_config_;
  envoy::api::v2::core::Metadata metadata_;
  std::unique_ptr<Envoy::Config::TypedMetadata> typed_metadata_;
};

class MockIdleTimeEnabledClusterInfo : public MockClusterInfo {
public:
  MockIdleTimeEnabledClusterInfo();
  ~MockIdleTimeEnabledClusterInfo() override;
};

} // namespace Upstream
} // namespace Envoy
