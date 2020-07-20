#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/time.h"
#include "envoy/config/config_provider.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/http/hash_policy.h"
#include "envoy/local_info/local_info.h"
#include "envoy/router/rds.h"
#include "envoy/router/route_config_provider_manager.h"
#include "envoy/router/router.h"
#include "envoy/router/router_ratelimit.h"
#include "envoy/router/scopes.h"
#include "envoy/router/shadow_writer.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stream_info/filter_state.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/type/v3/percent.pb.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/stats/fake_symbol_table_impl.h"

#include "test/mocks/stats/mocks.h"
#include "test/test_common/global.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Router {
using ::testing::NiceMock;

class MockDirectResponseEntry : public DirectResponseEntry {
public:
  MockDirectResponseEntry();
  ~MockDirectResponseEntry() override;

  // DirectResponseEntry
  MOCK_METHOD(void, finalizeResponseHeaders,
              (Http::ResponseHeaderMap & headers, const StreamInfo::StreamInfo& stream_info),
              (const));
  MOCK_METHOD(std::string, newPath, (const Http::RequestHeaderMap& headers), (const));
  MOCK_METHOD(void, rewritePathHeader,
              (Http::RequestHeaderMap & headers, bool insert_envoy_original_path), (const));
  MOCK_METHOD(Http::Code, responseCode, (), (const));
  MOCK_METHOD(const std::string&, responseBody, (), (const));
  MOCK_METHOD(const std::string&, routeName, (), (const));
};

class TestCorsPolicy : public CorsPolicy {
public:
  // Router::CorsPolicy
  const std::vector<Matchers::StringMatcherPtr>& allowOrigins() const override {
    return allow_origins_;
  };
  const std::string& allowMethods() const override { return allow_methods_; };
  const std::string& allowHeaders() const override { return allow_headers_; };
  const std::string& exposeHeaders() const override { return expose_headers_; };
  const std::string& maxAge() const override { return max_age_; };
  const absl::optional<bool>& allowCredentials() const override { return allow_credentials_; };
  bool enabled() const override { return enabled_; };
  bool shadowEnabled() const override { return shadow_enabled_; };

  std::vector<Matchers::StringMatcherPtr> allow_origins_;
  std::string allow_methods_;
  std::string allow_headers_;
  std::string expose_headers_;
  std::string max_age_{};
  absl::optional<bool> allow_credentials_;
  bool enabled_{};
  bool shadow_enabled_{};
};

class TestHedgePolicy : public HedgePolicy {
public:
  // Router::HedgePolicy
  uint32_t initialRequests() const override { return initial_requests_; }
  const envoy::type::v3::FractionalPercent& additionalRequestChance() const override {
    return additional_request_chance_;
  }
  bool hedgeOnPerTryTimeout() const override { return hedge_on_per_try_timeout_; }

  uint32_t initial_requests_{};
  envoy::type::v3::FractionalPercent additional_request_chance_{};
  bool hedge_on_per_try_timeout_{};
};

class TestRetryPolicy : public RetryPolicy {
public:
  TestRetryPolicy();
  ~TestRetryPolicy() override;

  // Router::RetryPolicy
  std::chrono::milliseconds perTryTimeout() const override { return per_try_timeout_; }
  uint32_t numRetries() const override { return num_retries_; }
  uint32_t retryOn() const override { return retry_on_; }
  MOCK_METHOD(std::vector<Upstream::RetryHostPredicateSharedPtr>, retryHostPredicates, (), (const));
  MOCK_METHOD(Upstream::RetryPrioritySharedPtr, retryPriority, (), (const));
  uint32_t hostSelectionMaxAttempts() const override { return host_selection_max_attempts_; }
  const std::vector<uint32_t>& retriableStatusCodes() const override {
    return retriable_status_codes_;
  }
  const std::vector<Http::HeaderMatcherSharedPtr>& retriableHeaders() const override {
    return retriable_headers_;
  }
  const std::vector<Http::HeaderMatcherSharedPtr>& retriableRequestHeaders() const override {
    return retriable_request_headers_;
  }

  absl::optional<std::chrono::milliseconds> baseInterval() const override { return base_interval_; }
  absl::optional<std::chrono::milliseconds> maxInterval() const override { return max_interval_; }
  absl::optional<std::chrono::milliseconds> rateLimitedResetMaxInterval() const override {
    return ratelimited_reset_max_interval_;
  }
  const std::vector<Http::HeaderMatcherSharedPtr>& rateLimitedResetHeaders() const override {
    return ratelimited_reset_headers_;
  }

  std::chrono::milliseconds per_try_timeout_{0};
  uint32_t num_retries_{};
  uint32_t retry_on_{};
  uint32_t host_selection_max_attempts_;
  std::vector<uint32_t> retriable_status_codes_;
  std::vector<Http::HeaderMatcherSharedPtr> retriable_headers_;
  std::vector<Http::HeaderMatcherSharedPtr> retriable_request_headers_;
  absl::optional<std::chrono::milliseconds> base_interval_{};
  absl::optional<std::chrono::milliseconds> max_interval_{};
  std::vector<Http::HeaderMatcherSharedPtr> ratelimited_reset_headers_;
  absl::optional<std::chrono::milliseconds> ratelimited_reset_max_interval_{};
};

class MockInternalRedirectPolicy : public InternalRedirectPolicy {
public:
  MockInternalRedirectPolicy();
  MOCK_METHOD(bool, enabled, (), (const));
  MOCK_METHOD(bool, shouldRedirectForResponseCode, (const Http::Code& response_code), (const));
  MOCK_METHOD(std::vector<InternalRedirectPredicateSharedPtr>, predicates, (), (const));
  MOCK_METHOD(uint32_t, maxInternalRedirects, (), (const));
  MOCK_METHOD(bool, isCrossSchemeRedirectAllowed, (), (const));
};

class MockInternalRedirectPredicate : public InternalRedirectPredicate {
public:
  MOCK_METHOD(bool, acceptTargetRoute, (StreamInfo::FilterState&, absl::string_view, bool, bool));
  MOCK_METHOD(absl::string_view, name, (), (const));
};

class MockRetryState : public RetryState {
public:
  MockRetryState();
  ~MockRetryState() override;

  void expectHeadersRetry();
  void expectHedgedPerTryTimeoutRetry();
  void expectResetRetry();

  MOCK_METHOD(bool, enabled, ());
  MOCK_METHOD(absl::optional<std::chrono::milliseconds>, parseRateLimitedResetInterval,
              (const Http::ResponseHeaderMap& response_headers), (const));
  MOCK_METHOD(RetryStatus, shouldRetryHeaders,
              (const Http::ResponseHeaderMap& response_headers, DoRetryCallback callback));
  MOCK_METHOD(bool, wouldRetryFromHeaders, (const Http::ResponseHeaderMap& response_headers));
  MOCK_METHOD(RetryStatus, shouldRetryReset,
              (const Http::StreamResetReason reset_reason, DoRetryCallback callback));
  MOCK_METHOD(RetryStatus, shouldHedgeRetryPerTryTimeout, (DoRetryCallback callback));
  MOCK_METHOD(void, onHostAttempted, (Upstream::HostDescriptionConstSharedPtr));
  MOCK_METHOD(bool, shouldSelectAnotherHost, (const Upstream::Host& host));
  MOCK_METHOD(const Upstream::HealthyAndDegradedLoad&, priorityLoadForRetry,
              (const Upstream::PrioritySet&, const Upstream::HealthyAndDegradedLoad&,
               const Upstream::RetryPriority::PriorityMappingFunc&));
  MOCK_METHOD(uint32_t, hostSelectionMaxAttempts, (), (const));
  MOCK_METHOD(const std::vector<Http::HeaderMatcherSharedPtr>&, rateLimitedResetHeaders, (),
              (const));
  MOCK_METHOD(std::chrono::milliseconds, rateLimitedResetMaxInterval, (), (const));

  DoRetryCallback callback_;
};

class MockRateLimitPolicyEntry : public RateLimitPolicyEntry {
public:
  MockRateLimitPolicyEntry();
  ~MockRateLimitPolicyEntry() override;

  // Router::RateLimitPolicyEntry
  MOCK_METHOD(uint64_t, stage, (), (const));
  MOCK_METHOD(const std::string&, disableKey, (), (const));
  MOCK_METHOD(void, populateDescriptors,
              (const RouteEntry& route, std::vector<Envoy::RateLimit::Descriptor>& descriptors,
               const std::string& local_service_cluster, const Http::HeaderMap& headers,
               const Network::Address::Instance& remote_address,
               const envoy::config::core::v3::Metadata* dynamic_metadata),
              (const));

  uint64_t stage_{};
  std::string disable_key_;
};

class MockRateLimitPolicy : public RateLimitPolicy {
public:
  MockRateLimitPolicy();
  ~MockRateLimitPolicy() override;

  // Router::RateLimitPolicy
  MOCK_METHOD(std::vector<std::reference_wrapper<const RateLimitPolicyEntry>>&,
              getApplicableRateLimit, (uint64_t stage), (const));
  MOCK_METHOD(bool, empty, (), (const));

  std::vector<std::reference_wrapper<const Router::RateLimitPolicyEntry>> rate_limit_policy_entry_;
};

class TestShadowPolicy : public ShadowPolicy {
public:
  TestShadowPolicy(absl::string_view cluster = "", absl::string_view runtime_key = "",
                   envoy::type::v3::FractionalPercent default_value = {}, bool trace_sampled = true)
      : cluster_(cluster), runtime_key_(runtime_key), default_value_(default_value),
        trace_sampled_(trace_sampled) {}
  // Router::ShadowPolicy
  const std::string& cluster() const override { return cluster_; }
  const std::string& runtimeKey() const override { return runtime_key_; }
  const envoy::type::v3::FractionalPercent& defaultValue() const override { return default_value_; }
  bool traceSampled() const override { return trace_sampled_; }

  std::string cluster_;
  std::string runtime_key_;
  envoy::type::v3::FractionalPercent default_value_;
  bool trace_sampled_;
};

class MockShadowWriter : public ShadowWriter {
public:
  MockShadowWriter();
  ~MockShadowWriter() override;

  // Router::ShadowWriter
  void shadow(const std::string& cluster, Http::RequestMessagePtr&& request,
              const Http::AsyncClient::RequestOptions& options) override {
    shadow_(cluster, request, options);
  }

  MOCK_METHOD(void, shadow_,
              (const std::string& cluster, Http::RequestMessagePtr& request,
               const Http::AsyncClient::RequestOptions& options));
};

class TestVirtualCluster : public VirtualCluster {
public:
  // Router::VirtualCluster
  Stats::StatName statName() const override { return stat_name_.statName(); }
  VirtualClusterStats& stats() const override { return stats_; }

  Stats::TestSymbolTable symbol_table_;
  Stats::StatNameManagedStorage stat_name_{"fake_virtual_cluster", *symbol_table_};
  Stats::IsolatedStoreImpl stats_store_;
  mutable VirtualClusterStats stats_{generateStats(stats_store_)};
};

class MockVirtualHost : public VirtualHost {
public:
  MockVirtualHost();
  ~MockVirtualHost() override;

  // Router::VirtualHost
  MOCK_METHOD(const std::string&, name, (), (const));
  MOCK_METHOD(const RateLimitPolicy&, rateLimitPolicy, (), (const));
  MOCK_METHOD(const CorsPolicy*, corsPolicy, (), (const));
  MOCK_METHOD(const Config&, routeConfig, (), (const));
  MOCK_METHOD(const RouteSpecificFilterConfig*, perFilterConfig, (const std::string&), (const));
  MOCK_METHOD(bool, includeAttemptCountInRequest, (), (const));
  MOCK_METHOD(bool, includeAttemptCountInResponse, (), (const));
  MOCK_METHOD(Upstream::RetryPrioritySharedPtr, retryPriority, ());
  MOCK_METHOD(Upstream::RetryHostPredicateSharedPtr, retryHostPredicate, ());
  MOCK_METHOD(uint32_t, retryShadowBufferLimit, (), (const));

  Stats::StatName statName() const override {
    stat_name_ = std::make_unique<Stats::StatNameManagedStorage>(name(), *symbol_table_);
    return stat_name_->statName();
  }

  mutable Stats::TestSymbolTable symbol_table_;
  std::string name_{"fake_vhost"};
  mutable std::unique_ptr<Stats::StatNameManagedStorage> stat_name_;
  testing::NiceMock<MockRateLimitPolicy> rate_limit_policy_;
  TestCorsPolicy cors_policy_;
};

class MockHashPolicy : public Http::HashPolicy {
public:
  MockHashPolicy();
  ~MockHashPolicy() override;

  // Http::HashPolicy
  MOCK_METHOD(absl::optional<uint64_t>, generateHash,
              (const Network::Address::Instance* downstream_address,
               const Http::RequestHeaderMap& headers, const AddCookieCallback add_cookie,
               const StreamInfo::FilterStateSharedPtr filter_state),
              (const));
};

class MockMetadataMatchCriteria : public MetadataMatchCriteria {
public:
  MockMetadataMatchCriteria();
  ~MockMetadataMatchCriteria() override;

  // Router::MetadataMatchCriteria
  MOCK_METHOD(const std::vector<MetadataMatchCriterionConstSharedPtr>&, metadataMatchCriteria, (),
              (const));
  MOCK_METHOD(MetadataMatchCriteriaConstPtr, mergeMatchCriteria, (const ProtobufWkt::Struct&),
              (const));
  MOCK_METHOD(MetadataMatchCriteriaConstPtr, filterMatchCriteria, (const std::set<std::string>&),
              (const));
};

class MockTlsContextMatchCriteria : public TlsContextMatchCriteria {
public:
  MockTlsContextMatchCriteria();
  ~MockTlsContextMatchCriteria() override;

  // Router::MockTlsContextMatchCriteria
  MOCK_METHOD(const absl::optional<bool>&, presented, (), (const));
  MOCK_METHOD(const absl::optional<bool>&, validated, (), (const));
};

class MockPathMatchCriterion : public PathMatchCriterion {
public:
  MockPathMatchCriterion();
  ~MockPathMatchCriterion() override;

  // Router::PathMatchCriterion
  MOCK_METHOD(PathMatchType, matchType, (), (const));
  MOCK_METHOD(const std::string&, matcher, (), (const));

  PathMatchType type_;
  std::string matcher_;
};

class MockRouteEntry : public RouteEntry {
public:
  MockRouteEntry();
  ~MockRouteEntry() override;

  // Router::Config
  MOCK_METHOD(const std::string&, clusterName, (), (const));
  MOCK_METHOD(Http::Code, clusterNotFoundResponseCode, (), (const));
  MOCK_METHOD(void, finalizeRequestHeaders,
              (Http::RequestHeaderMap & headers, const StreamInfo::StreamInfo& stream_info,
               bool insert_envoy_original_path),
              (const));
  MOCK_METHOD(void, finalizeResponseHeaders,
              (Http::ResponseHeaderMap & headers, const StreamInfo::StreamInfo& stream_info),
              (const));
  MOCK_METHOD(const Http::HashPolicy*, hashPolicy, (), (const));
  MOCK_METHOD(const HedgePolicy&, hedgePolicy, (), (const));
  MOCK_METHOD(const Router::MetadataMatchCriteria*, metadataMatchCriteria, (), (const));
  MOCK_METHOD(const Router::TlsContextMatchCriteria*, tlsContextMatchCriteria, (), (const));
  MOCK_METHOD(Upstream::ResourcePriority, priority, (), (const));
  MOCK_METHOD(const RateLimitPolicy&, rateLimitPolicy, (), (const));
  MOCK_METHOD(const RetryPolicy&, retryPolicy, (), (const));
  MOCK_METHOD(const InternalRedirectPolicy&, internalRedirectPolicy, (), (const));
  MOCK_METHOD(uint32_t, retryShadowBufferLimit, (), (const));
  MOCK_METHOD(const std::vector<ShadowPolicyPtr>&, shadowPolicies, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, timeout, (), (const));
  MOCK_METHOD(absl::optional<std::chrono::milliseconds>, idleTimeout, (), (const));
  MOCK_METHOD(absl::optional<std::chrono::milliseconds>, maxGrpcTimeout, (), (const));
  MOCK_METHOD(absl::optional<std::chrono::milliseconds>, grpcTimeoutOffset, (), (const));
  MOCK_METHOD(const VirtualCluster*, virtualCluster, (const Http::HeaderMap& headers), (const));
  MOCK_METHOD(const std::string&, virtualHostName, (), (const));
  MOCK_METHOD(const VirtualHost&, virtualHost, (), (const));
  MOCK_METHOD(bool, autoHostRewrite, (), (const));
  MOCK_METHOD((const std::multimap<std::string, std::string>&), opaqueConfig, (), (const));
  MOCK_METHOD(bool, includeVirtualHostRateLimits, (), (const));
  MOCK_METHOD(const CorsPolicy*, corsPolicy, (), (const));
  MOCK_METHOD(const envoy::config::core::v3::Metadata&, metadata, (), (const));
  MOCK_METHOD(const Envoy::Config::TypedMetadata&, typedMetadata, (), (const));
  MOCK_METHOD(const PathMatchCriterion&, pathMatchCriterion, (), (const));
  MOCK_METHOD(const RouteSpecificFilterConfig*, perFilterConfig, (const std::string&), (const));
  MOCK_METHOD(bool, includeAttemptCountInRequest, (), (const));
  MOCK_METHOD(bool, includeAttemptCountInResponse, (), (const));
  MOCK_METHOD(const absl::optional<ConnectConfig>&, connectConfig, (), (const));
  MOCK_METHOD(const UpgradeMap&, upgradeMap, (), (const));
  MOCK_METHOD(const std::string&, routeName, (), (const));

  std::string cluster_name_{"fake_cluster"};
  std::string route_name_{"fake_route_name"};
  std::multimap<std::string, std::string> opaque_config_;
  TestVirtualCluster virtual_cluster_;
  TestRetryPolicy retry_policy_;
  testing::NiceMock<MockInternalRedirectPolicy> internal_redirect_policy_;
  TestHedgePolicy hedge_policy_;
  testing::NiceMock<MockRateLimitPolicy> rate_limit_policy_;
  std::vector<ShadowPolicyPtr> shadow_policies_;
  testing::NiceMock<MockVirtualHost> virtual_host_;
  MockHashPolicy hash_policy_;
  MockMetadataMatchCriteria metadata_matches_criteria_;
  MockTlsContextMatchCriteria tls_context_matches_criteria_;
  TestCorsPolicy cors_policy_;
  testing::NiceMock<MockPathMatchCriterion> path_match_criterion_;
  envoy::config::core::v3::Metadata metadata_;
  UpgradeMap upgrade_map_;
  absl::optional<ConnectConfig> connect_config_;
};

class MockDecorator : public Decorator {
public:
  MockDecorator();
  ~MockDecorator() override;

  // Router::Decorator
  MOCK_METHOD(const std::string&, getOperation, (), (const));
  MOCK_METHOD(bool, propagate, (), (const));
  MOCK_METHOD(void, apply, (Tracing::Span & span), (const));

  std::string operation_{"fake_operation"};
};

class MockRouteTracing : public RouteTracing {
public:
  MockRouteTracing();
  ~MockRouteTracing() override;

  // Router::RouteTracing
  MOCK_METHOD(const envoy::type::v3::FractionalPercent&, getClientSampling, (), (const));
  MOCK_METHOD(const envoy::type::v3::FractionalPercent&, getRandomSampling, (), (const));
  MOCK_METHOD(const envoy::type::v3::FractionalPercent&, getOverallSampling, (), (const));
  MOCK_METHOD(const Tracing::CustomTagMap&, getCustomTags, (), (const));
};

class MockRoute : public Route {
public:
  MockRoute();
  ~MockRoute() override;

  // Router::Route
  MOCK_METHOD(const DirectResponseEntry*, directResponseEntry, (), (const));
  MOCK_METHOD(const RouteEntry*, routeEntry, (), (const));
  MOCK_METHOD(const Decorator*, decorator, (), (const));
  MOCK_METHOD(const RouteTracing*, tracingConfig, (), (const));
  MOCK_METHOD(const RouteSpecificFilterConfig*, perFilterConfig, (const std::string&), (const));

  testing::NiceMock<MockRouteEntry> route_entry_;
  testing::NiceMock<MockDecorator> decorator_;
  testing::NiceMock<MockRouteTracing> route_tracing_;
};

class MockConfig : public Config {
public:
  MockConfig();
  ~MockConfig() override;

  // Router::Config
  MOCK_METHOD(RouteConstSharedPtr, route,
              (const Http::RequestHeaderMap&, const Envoy::StreamInfo::StreamInfo&,
               uint64_t random_value),
              (const));
  MOCK_METHOD(RouteConstSharedPtr, route,
              (const RouteCallback& cb, const Http::RequestHeaderMap&,
               const Envoy::StreamInfo::StreamInfo&, uint64_t random_value),
              (const));

  MOCK_METHOD(const std::list<Http::LowerCaseString>&, internalOnlyHeaders, (), (const));
  MOCK_METHOD(const std::string&, name, (), (const));
  MOCK_METHOD(bool, usesVhds, (), (const));
  MOCK_METHOD(bool, mostSpecificHeaderMutationsWins, (), (const));

  std::shared_ptr<MockRoute> route_;
  std::list<Http::LowerCaseString> internal_only_headers_;
  std::string name_{"fake_config"};
};

class MockRouteConfigProvider : public RouteConfigProvider {
public:
  MockRouteConfigProvider();
  ~MockRouteConfigProvider() override;

  MOCK_METHOD(ConfigConstSharedPtr, config, ());
  MOCK_METHOD(absl::optional<ConfigInfo>, configInfo, (), (const));
  MOCK_METHOD(SystemTime, lastUpdated, (), (const));
  MOCK_METHOD(void, onConfigUpdate, ());
  MOCK_METHOD(void, validateConfig, (const envoy::config::route::v3::RouteConfiguration&), (const));
  MOCK_METHOD(void, requestVirtualHostsUpdate,
              (const std::string&, Event::Dispatcher&,
               std::weak_ptr<Http::RouteConfigUpdatedCallback> route_config_updated_cb));

  std::shared_ptr<NiceMock<MockConfig>> route_config_{new NiceMock<MockConfig>()};
};

class MockRouteConfigProviderManager : public RouteConfigProviderManager {
public:
  MockRouteConfigProviderManager();
  ~MockRouteConfigProviderManager() override;

  MOCK_METHOD(RouteConfigProviderSharedPtr, createRdsRouteConfigProvider,
              (const envoy::extensions::filters::network::http_connection_manager::v3::Rds& rds,
               Server::Configuration::ServerFactoryContext& factory_context,
               const std::string& stat_prefix, Init::Manager& init_manager));
  MOCK_METHOD(RouteConfigProviderPtr, createStaticRouteConfigProvider,
              (const envoy::config::route::v3::RouteConfiguration& route_config,
               Server::Configuration::ServerFactoryContext& factory_context,
               ProtobufMessage::ValidationVisitor& validator));
};

class MockScopedConfig : public ScopedConfig {
public:
  MockScopedConfig();
  ~MockScopedConfig() override;

  MOCK_METHOD(ConfigConstSharedPtr, getRouteConfig, (const Http::HeaderMap& headers), (const));

  std::shared_ptr<MockConfig> route_config_{new NiceMock<MockConfig>()};
};

class MockScopedRouteConfigProvider : public Envoy::Config::ConfigProvider {
public:
  MockScopedRouteConfigProvider();
  ~MockScopedRouteConfigProvider() override;

  // Config::ConfigProvider
  MOCK_METHOD(SystemTime, lastUpdated, (), (const));
  MOCK_METHOD(Protobuf::Message*, getConfigProto, (), (const));
  MOCK_METHOD(Envoy::Config::ConfigProvider::ConfigProtoVector, getConfigProtos, (), (const));
  MOCK_METHOD(ConfigConstSharedPtr, getConfig, (), (const));
  MOCK_METHOD(ApiType, apiType, (), (const));

  std::shared_ptr<MockScopedConfig> config_;
};

class MockGenericConnPool : public GenericConnPool {
  MOCK_METHOD(void, newStream, (GenericConnectionPoolCallbacks * request));
  MOCK_METHOD(bool, cancelAnyPendingRequest, ());
  MOCK_METHOD(absl::optional<Http::Protocol>, protocol, (), (const));
  MOCK_METHOD(bool, initialize,
              (Upstream::ClusterManager&, const RouteEntry&, Http::Protocol,
               Upstream::LoadBalancerContext*));
  MOCK_METHOD(Upstream::HostDescriptionConstSharedPtr, host, (), (const));
};

class MockUpstreamToDownstream : public UpstreamToDownstream {
public:
  MOCK_METHOD(const RouteEntry&, routeEntry, (), (const));
  MOCK_METHOD(const Network::Connection&, connection, (), (const));

  MOCK_METHOD(void, decodeData, (Buffer::Instance&, bool));
  MOCK_METHOD(void, decodeMetadata, (Http::MetadataMapPtr &&));
  MOCK_METHOD(void, decode100ContinueHeaders, (Http::ResponseHeaderMapPtr &&));
  MOCK_METHOD(void, decodeHeaders, (Http::ResponseHeaderMapPtr&&, bool));
  MOCK_METHOD(void, decodeTrailers, (Http::ResponseTrailerMapPtr &&));

  MOCK_METHOD(void, onResetStream, (Http::StreamResetReason, absl::string_view));
  MOCK_METHOD(void, onAboveWriteBufferHighWatermark, ());
  MOCK_METHOD(void, onBelowWriteBufferLowWatermark, ());
};

class MockGenericConnectionPoolCallbacks : public GenericConnectionPoolCallbacks {
public:
  MockGenericConnectionPoolCallbacks();

  MOCK_METHOD(void, onPoolFailure,
              (Http::ConnectionPool::PoolFailureReason reason,
               absl::string_view transport_failure_reason,
               Upstream::HostDescriptionConstSharedPtr host));
  MOCK_METHOD(void, onPoolReady,
              (std::unique_ptr<GenericUpstream> && upstream,
               Upstream::HostDescriptionConstSharedPtr host,
               const Network::Address::InstanceConstSharedPtr& upstream_local_address,
               const StreamInfo::StreamInfo& info));
  MOCK_METHOD(UpstreamToDownstream&, upstreamToDownstream, ());

  NiceMock<MockUpstreamToDownstream> upstream_to_downstream_;
};

} // namespace Router
} // namespace Envoy
