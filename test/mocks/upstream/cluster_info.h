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

#include "source/common/common/thread.h"
#include "source/common/http/http1/codec_stats.h"
#include "source/common/http/http2/codec_stats.h"
#include "source/common/http/http3/codec_stats.h"
#include "source/common/upstream/upstream_impl.h"

#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/transport_socket_match.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Http {
class FilterChainManager;
}

namespace Upstream {

// While this mock class doesn't have any direct use in public Envoy tests, it's
// useful for constructing tests of downstream private filters that use
// ClusterTypedMetadata.
class MockClusterTypedMetadata : public Config::TypedMetadataImpl<ClusterTypedMetadataFactory> {
public:
  using Config::TypedMetadataImpl<ClusterTypedMetadataFactory>::TypedMetadataImpl;

  void inject(const std::string& key, std::unique_ptr<const TypedMetadata::Object> value) {
    data_[key] = std::move(value);
  }

  absl::node_hash_map<std::string, std::unique_ptr<const TypedMetadata::Object>>& data() {
    return data_;
  }
};

class MockUpstreamLocalAddressSelector : public UpstreamLocalAddressSelector {
public:
  MockUpstreamLocalAddressSelector(Network::Address::InstanceConstSharedPtr& address);

  MOCK_METHOD(UpstreamLocalAddress, getUpstreamLocalAddressImpl,
              (const Network::Address::InstanceConstSharedPtr& address), (const));

  Network::Address::InstanceConstSharedPtr& address_;
};

class MockUpstreamLocalAddressSelectorFactory : public UpstreamLocalAddressSelectorFactory {
public:
  MOCK_METHOD(absl::StatusOr<UpstreamLocalAddressSelectorConstSharedPtr>,
              createLocalAddressSelector,
              (std::vector<::Envoy::Upstream::UpstreamLocalAddress> upstream_local_addresses,
               absl::optional<std::string> cluster_name),
              (const));

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::Empty>();
  }

  std::string name() const override { return "mock.upstream.local.address.selector"; }
};

class MockClusterInfo : public ClusterInfo {
public:
  MockClusterInfo();
  ~MockClusterInfo() override;

  void resetResourceManager(uint64_t cx, uint64_t rq_pending, uint64_t rq, uint64_t rq_retry,
                            uint64_t conn_pool, uint64_t conn_per_host = 100) {
    resource_manager_ = std::make_unique<ResourceManagerImpl>(
        runtime_, name_, cx, rq_pending, rq, rq_retry, conn_pool, conn_per_host,
        circuit_breakers_stats_, absl::nullopt, absl::nullopt);
  }

  void resetResourceManagerWithRetryBudget(uint64_t cx, uint64_t rq_pending, uint64_t rq,
                                           uint64_t rq_retry, uint64_t conn_pool,
                                           double budget_percent, uint32_t min_retry_concurrency,
                                           uint64_t conn_per_host = 100) {
    resource_manager_ = std::make_unique<ResourceManagerImpl>(
        runtime_, name_, cx, rq_pending, rq, rq_retry, conn_pool, conn_per_host,
        circuit_breakers_stats_, budget_percent, min_retry_concurrency);
  }

  // Upstream::ClusterInfo
  MOCK_METHOD(bool, addedViaApi, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, connectTimeout, (), (const));
  MOCK_METHOD(const absl::optional<std::chrono::milliseconds>, idleTimeout, (), (const));
  MOCK_METHOD(const absl::optional<std::chrono::milliseconds>, tcpPoolIdleTimeout, (), (const));
  MOCK_METHOD(const absl::optional<std::chrono::milliseconds>, maxConnectionDuration, (), (const));
  MOCK_METHOD(const absl::optional<std::chrono::milliseconds>, maxStreamDuration, (), (const));
  MOCK_METHOD(const absl::optional<std::chrono::milliseconds>, grpcTimeoutHeaderMax, (), (const));
  MOCK_METHOD(const absl::optional<std::chrono::milliseconds>, grpcTimeoutHeaderOffset, (),
              (const));
  MOCK_METHOD(float, perUpstreamPreconnectRatio, (), (const));
  MOCK_METHOD(float, peekaheadRatio, (), (const));
  MOCK_METHOD(uint32_t, perConnectionBufferLimitBytes, (), (const));
  MOCK_METHOD(uint64_t, features, (), (const));
  MOCK_METHOD(const Http::Http1Settings&, http1Settings, (), (const));
  MOCK_METHOD(const envoy::config::core::v3::Http2ProtocolOptions&, http2Options, (), (const));
  MOCK_METHOD(const envoy::config::core::v3::Http3ProtocolOptions&, http3Options, (), (const));
  MOCK_METHOD(const envoy::config::core::v3::HttpProtocolOptions&, commonHttpProtocolOptions, (),
              (const));
  MOCK_METHOD(ProtocolOptionsConfigConstSharedPtr, extensionProtocolOptions, (const std::string&),
              (const));
  MOCK_METHOD(OptRef<const LoadBalancerConfig>, loadBalancerConfig, (), (const));
  MOCK_METHOD(TypedLoadBalancerFactory&, loadBalancerFactory, (), (const));
  MOCK_METHOD(const envoy::config::cluster::v3::Cluster::CommonLbConfig&, lbConfig, (), (const));
  MOCK_METHOD(envoy::config::cluster::v3::Cluster::DiscoveryType, type, (), (const));
  MOCK_METHOD(OptRef<const envoy::config::cluster::v3::Cluster::CustomClusterType>, clusterType, (),
              (const));
  MOCK_METHOD(OptRef<const envoy::config::core::v3::TypedExtensionConfig>, upstreamConfig, (),
              (const));
  MOCK_METHOD(bool, maintenanceMode, (), (const));
  MOCK_METHOD(uint32_t, maxResponseHeadersCount, (), (const));
  MOCK_METHOD(absl::optional<uint16_t>, maxResponseHeadersKb, (), (const));
  MOCK_METHOD(uint64_t, maxRequestsPerConnection, (), (const));
  MOCK_METHOD(const std::string&, name, (), (const));
  MOCK_METHOD(const std::string&, observabilityName, (), (const));
  MOCK_METHOD(ResourceManager&, resourceManager, (ResourcePriority priority), (const));
  MOCK_METHOD(TransportSocketMatcher&, transportSocketMatcher, (), (const));
  MOCK_METHOD(DeferredCreationCompatibleClusterTrafficStats&, trafficStats, (), (const));
  MOCK_METHOD(ClusterLbStats&, lbStats, (), (const));
  MOCK_METHOD(ClusterEndpointStats&, endpointStats, (), (const));
  MOCK_METHOD(ClusterConfigUpdateStats&, configUpdateStats, (), (const));
  MOCK_METHOD(Stats::Scope&, statsScope, (), (const));
  MOCK_METHOD(ClusterLoadReportStats&, loadReportStats, (), (const));
  MOCK_METHOD(ClusterRequestResponseSizeStatsOptRef, requestResponseSizeStats, (), (const));
  MOCK_METHOD(ClusterTimeoutBudgetStatsOptRef, timeoutBudgetStats, (), (const));
  MOCK_METHOD(bool, perEndpointStatsEnabled, (), (const));
  MOCK_METHOD(UpstreamLocalAddressSelectorConstSharedPtr, getUpstreamLocalAddressSelector, (),
              (const));
  MOCK_METHOD(const envoy::config::core::v3::Metadata&, metadata, (), (const));
  MOCK_METHOD(const Envoy::Config::TypedMetadata&, typedMetadata, (), (const));
  MOCK_METHOD(bool, drainConnectionsOnHostRemoval, (), (const));
  MOCK_METHOD(bool, connectionPoolPerDownstreamConnection, (), (const));
  MOCK_METHOD(bool, warmHosts, (), (const));
  MOCK_METHOD(bool, setLocalInterfaceNameOnUpstreamConnections, (), (const));
  MOCK_METHOD(const absl::optional<envoy::config::core::v3::UpstreamHttpProtocolOptions>&,
              upstreamHttpProtocolOptions, (), (const));
  MOCK_METHOD(const absl::optional<const envoy::config::core::v3::AlternateProtocolsCacheOptions>&,
              alternateProtocolsCacheOptions, (), (const));
  MOCK_METHOD(const std::string&, edsServiceName, (), (const));
  MOCK_METHOD(void, createNetworkFilterChain, (Network::Connection&), (const));
  MOCK_METHOD(std::vector<Http::Protocol>, upstreamHttpProtocol, (absl::optional<Http::Protocol>),
              (const));

  MOCK_METHOD(bool, createFilterChain,
              (Http::FilterChainManager & manager, bool only_create_if_configured,
               const Http::FilterChainOptions& options),
              (const, override));
  MOCK_METHOD(bool, createUpgradeFilterChain,
              (absl::string_view upgrade_type,
               const Http::FilterChainFactory::UpgradeMap* upgrade_map,
               Http::FilterChainManager& manager, const Http::FilterChainOptions&),
              (const));
  MOCK_METHOD(Http::ClientHeaderValidatorPtr, makeHeaderValidator, (Http::Protocol), (const));
  MOCK_METHOD(
      OptRef<const envoy::config::cluster::v3::UpstreamConnectionOptions::HappyEyeballsConfig>,
      happyEyeballsConfig, (), (const));
  MOCK_METHOD(OptRef<const std::vector<std::string>>, lrsReportMetricNames, (), (const));
  ::Envoy::Http::HeaderValidatorStats& codecStats(Http::Protocol protocol) const;
  Http::Http1::CodecStats& http1CodecStats() const override;
  Http::Http2::CodecStats& http2CodecStats() const override;
  Http::Http3::CodecStats& http3CodecStats() const override;

  std::string name_{"fake_cluster"};
  std::string observability_name_{"observability_name"};
  absl::optional<std::string> eds_service_name_;
  Http::Http1Settings http1_settings_;
  envoy::config::core::v3::Http2ProtocolOptions http2_options_;
  envoy::config::core::v3::Http3ProtocolOptions http3_options_;
  envoy::config::core::v3::HttpProtocolOptions common_http_protocol_options_;
  ProtocolOptionsConfigConstSharedPtr extension_protocol_options_;
  uint64_t max_requests_per_connection_{};
  uint32_t max_response_headers_count_{Http::DEFAULT_MAX_HEADERS_COUNT};
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  ClusterTrafficStatNames traffic_stat_names_;
  ClusterConfigUpdateStatNames config_update_stats_names_;
  ClusterLbStatNames lb_stat_names_;
  ClusterEndpointStatNames endpoint_stat_names_;
  ClusterLoadReportStatNames cluster_load_report_stat_names_;
  ClusterCircuitBreakersStatNames cluster_circuit_breakers_stat_names_;
  ClusterRequestResponseSizeStatNames cluster_request_response_size_stat_names_;
  ClusterTimeoutBudgetStatNames cluster_timeout_budget_stat_names_;
  mutable DeferredCreationCompatibleClusterTrafficStats traffic_stats_;
  ClusterConfigUpdateStats config_update_stats_;
  ClusterLbStats lb_stats_;
  ClusterEndpointStats endpoint_stats_;
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
  std::shared_ptr<MockUpstreamLocalAddressSelector> upstream_local_address_selector_;
  envoy::config::cluster::v3::Cluster::DiscoveryType type_{
      envoy::config::cluster::v3::Cluster::STRICT_DNS};
  std::unique_ptr<const envoy::config::cluster::v3::Cluster::CustomClusterType> cluster_type_;
  absl::optional<envoy::config::core::v3::UpstreamHttpProtocolOptions>
      upstream_http_protocol_options_;
  absl::optional<const envoy::config::core::v3::AlternateProtocolsCacheOptions>
      alternate_protocols_cache_options_;
  Upstream::TypedLoadBalancerFactory* lb_factory_ =
      Config::Utility::getFactoryByName<Upstream::TypedLoadBalancerFactory>(
          "envoy.load_balancing_policies.round_robin");
  std::unique_ptr<envoy::config::core::v3::TypedExtensionConfig> upstream_config_;
  Network::ConnectionSocket::OptionsSharedPtr cluster_socket_options_;
  envoy::config::cluster::v3::Cluster::CommonLbConfig lb_config_;
  envoy::config::core::v3::Metadata metadata_;
  std::unique_ptr<Envoy::Config::TypedMetadata> typed_metadata_;
  absl::optional<std::chrono::milliseconds> max_stream_duration_;
  Stats::ScopeSharedPtr stats_scope_;
  mutable Http::Http1::CodecStats::AtomicPtr http1_codec_stats_;
  mutable Http::Http2::CodecStats::AtomicPtr http2_codec_stats_;
  mutable Http::Http3::CodecStats::AtomicPtr http3_codec_stats_;
  Http::HeaderValidatorFactoryPtr header_validator_factory_;
  absl::optional<envoy::config::cluster::v3::UpstreamConnectionOptions::HappyEyeballsConfig>
      happy_eyeballs_config_;
  const std::unique_ptr<Envoy::Orca::LrsReportMetricNames> lrs_report_metric_names_;
};

class MockIdleTimeEnabledClusterInfo : public MockClusterInfo {
public:
  MockIdleTimeEnabledClusterInfo();
  ~MockIdleTimeEnabledClusterInfo() override;
};

class MockMaxConnectionDurationEnabledClusterInfo : public MockClusterInfo {
public:
  MockMaxConnectionDurationEnabledClusterInfo();
  ~MockMaxConnectionDurationEnabledClusterInfo() override;
};

} // namespace Upstream
} // namespace Envoy
