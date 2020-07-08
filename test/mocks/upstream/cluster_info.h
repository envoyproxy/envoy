#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "common/common/thread.h"
#include "common/http/http1/codec_stats.h"
#include "common/http/http2/codec_stats.h"
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
  MOCK_METHOD(bool, isEnabled, (), (const));
  MOCK_METHOD(envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetFallbackPolicy,
              fallbackPolicy, (), (const));
  MOCK_METHOD(const ProtobufWkt::Struct&, defaultSubset, (), (const));
  MOCK_METHOD(const std::vector<SubsetSelectorPtr>&, subsetSelectors, (), (const));
  MOCK_METHOD(bool, localityWeightAware, (), (const));
  MOCK_METHOD(bool, scaleLocalityWeight, (), (const));
  MOCK_METHOD(bool, panicModeAny, (), (const));
  MOCK_METHOD(bool, listAsAny, (), (const));

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
        runtime_, name_, cx, rq_pending, rq, rq_retry, conn_pool, circuit_breakers_stats_,
        absl::nullopt, absl::nullopt);
  }

  void resetResourceManagerWithRetryBudget(uint64_t cx, uint64_t rq_pending, uint64_t rq,
                                           uint64_t rq_retry, uint64_t conn_pool,
                                           double budget_percent, uint32_t min_retry_concurrency) {
    resource_manager_ = std::make_unique<ResourceManagerImpl>(
        runtime_, name_, cx, rq_pending, rq, rq_retry, conn_pool, circuit_breakers_stats_,
        budget_percent, min_retry_concurrency);
  }

  // Upstream::ClusterInfo
  MOCK_METHOD(bool, addedViaApi, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, connectTimeout, (), (const));
  MOCK_METHOD(const absl::optional<std::chrono::milliseconds>, idleTimeout, (), (const));
  MOCK_METHOD(uint32_t, perConnectionBufferLimitBytes, (), (const));
  MOCK_METHOD(uint64_t, features, (), (const));
  MOCK_METHOD(const Http::Http1Settings&, http1Settings, (), (const));
  MOCK_METHOD(const envoy::config::core::v3::Http2ProtocolOptions&, http2Options, (), (const));
  MOCK_METHOD(const envoy::config::core::v3::HttpProtocolOptions&, commonHttpProtocolOptions, (),
              (const));
  MOCK_METHOD(ProtocolOptionsConfigConstSharedPtr, extensionProtocolOptions, (const std::string&),
              (const));
  MOCK_METHOD(const envoy::config::cluster::v3::Cluster::CommonLbConfig&, lbConfig, (), (const));
  MOCK_METHOD(LoadBalancerType, lbType, (), (const));
  MOCK_METHOD(envoy::config::cluster::v3::Cluster::DiscoveryType, type, (), (const));
  MOCK_METHOD(const absl::optional<envoy::config::cluster::v3::Cluster::CustomClusterType>&,
              clusterType, (), (const));
  MOCK_METHOD(const absl::optional<envoy::config::cluster::v3::Cluster::RingHashLbConfig>&,
              lbRingHashConfig, (), (const));
  MOCK_METHOD(const absl::optional<envoy::config::cluster::v3::Cluster::LeastRequestLbConfig>&,
              lbLeastRequestConfig, (), (const));
  MOCK_METHOD(const absl::optional<envoy::config::cluster::v3::Cluster::OriginalDstLbConfig>&,
              lbOriginalDstConfig, (), (const));
  MOCK_METHOD(const absl::optional<envoy::config::core::v3::TypedExtensionConfig>&, upstreamConfig,
              (), (const));
  MOCK_METHOD(bool, maintenanceMode, (), (const));
  MOCK_METHOD(uint32_t, maxResponseHeadersCount, (), (const));
  MOCK_METHOD(uint64_t, maxRequestsPerConnection, (), (const));
  MOCK_METHOD(const std::string&, name, (), (const));
  MOCK_METHOD(ResourceManager&, resourceManager, (ResourcePriority priority), (const));
  MOCK_METHOD(TransportSocketMatcher&, transportSocketMatcher, (), (const));
  MOCK_METHOD(ClusterStats&, stats, (), (const));
  MOCK_METHOD(Stats::Scope&, statsScope, (), (const));
  MOCK_METHOD(ClusterLoadReportStats&, loadReportStats, (), (const));
  MOCK_METHOD(ClusterRequestResponseSizeStatsOptRef, requestResponseSizeStats, (), (const));
  MOCK_METHOD(ClusterTimeoutBudgetStatsOptRef, timeoutBudgetStats, (), (const));
  MOCK_METHOD(const Network::Address::InstanceConstSharedPtr&, sourceAddress, (), (const));
  MOCK_METHOD(const LoadBalancerSubsetInfo&, lbSubsetInfo, (), (const));
  MOCK_METHOD(const envoy::config::core::v3::Metadata&, metadata, (), (const));
  MOCK_METHOD(const Envoy::Config::TypedMetadata&, typedMetadata, (), (const));
  MOCK_METHOD(const Network::ConnectionSocket::OptionsSharedPtr&, clusterSocketOptions, (),
              (const));
  MOCK_METHOD(bool, drainConnectionsOnHostRemoval, (), (const));
  MOCK_METHOD(bool, warmHosts, (), (const));
  MOCK_METHOD(const absl::optional<envoy::config::core::v3::UpstreamHttpProtocolOptions>&,
              upstreamHttpProtocolOptions, (), (const));
  MOCK_METHOD(absl::optional<std::string>, edsServiceName, (), (const));
  MOCK_METHOD(void, createNetworkFilterChain, (Network::Connection&), (const));
  MOCK_METHOD(Http::Protocol, upstreamHttpProtocol, (absl::optional<Http::Protocol>), (const));

  Http::Http1::CodecStats& http1CodecStats() const override;
  Http::Http2::CodecStats& http2CodecStats() const override;

  std::string name_{"fake_cluster"};
  absl::optional<std::string> eds_service_name_;
  Http::Http1Settings http1_settings_;
  envoy::config::core::v3::Http2ProtocolOptions http2_options_;
  envoy::config::core::v3::HttpProtocolOptions common_http_protocol_options_;
  ProtocolOptionsConfigConstSharedPtr extension_protocol_options_;
  uint64_t max_requests_per_connection_{};
  uint32_t max_response_headers_count_{Http::DEFAULT_MAX_HEADERS_COUNT};
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  ClusterStats stats_;
  Upstream::TransportSocketMatcherPtr transport_socket_matcher_;
  NiceMock<Stats::MockIsolatedStatsStore> load_report_stats_store_;
  ClusterLoadReportStats load_report_stats_;
  NiceMock<Stats::MockIsolatedStatsStore> request_response_size_stats_store_;
  ClusterRequestResponseSizeStatsPtr request_response_size_stats_;
  NiceMock<Stats::MockIsolatedStatsStore> timeout_budget_stats_store_;
  ClusterTimeoutBudgetStatsPtr timeout_budget_stats_;
  ClusterCircuitBreakersStats circuit_breakers_stats_;
  NiceMock<Runtime::MockLoader> runtime_;
  std::unique_ptr<Upstream::ResourceManager> resource_manager_;
  Network::Address::InstanceConstSharedPtr source_address_;
  LoadBalancerType lb_type_{LoadBalancerType::RoundRobin};
  envoy::config::cluster::v3::Cluster::DiscoveryType type_{
      envoy::config::cluster::v3::Cluster::STRICT_DNS};
  absl::optional<envoy::config::cluster::v3::Cluster::CustomClusterType> cluster_type_;
  NiceMock<MockLoadBalancerSubsetInfo> lb_subset_;
  absl::optional<envoy::config::core::v3::UpstreamHttpProtocolOptions>
      upstream_http_protocol_options_;
  absl::optional<envoy::config::cluster::v3::Cluster::RingHashLbConfig> lb_ring_hash_config_;
  absl::optional<envoy::config::cluster::v3::Cluster::OriginalDstLbConfig> lb_original_dst_config_;
  absl::optional<envoy::config::core::v3::TypedExtensionConfig> upstream_config_;
  Network::ConnectionSocket::OptionsSharedPtr cluster_socket_options_;
  envoy::config::cluster::v3::Cluster::CommonLbConfig lb_config_;
  envoy::config::core::v3::Metadata metadata_;
  std::unique_ptr<Envoy::Config::TypedMetadata> typed_metadata_;
  absl::optional<std::chrono::milliseconds> max_stream_duration_;
  mutable Http::Http1::CodecStats::AtomicPtr http1_codec_stats_;
  mutable Http::Http2::CodecStats::AtomicPtr http2_codec_stats_;
};

class MockIdleTimeEnabledClusterInfo : public MockClusterInfo {
public:
  MockIdleTimeEnabledClusterInfo();
  ~MockIdleTimeEnabledClusterInfo() override;
};

} // namespace Upstream
} // namespace Envoy
