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
#include "envoy/config/typed_metadata.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/hash_policy.h"
#include "envoy/local_info/local_info.h"
#include "envoy/router/rds.h"
#include "envoy/router/route_config_provider_manager.h"
#include "envoy/router/router.h"
#include "envoy/router/router_ratelimit.h"
#include "envoy/router/scopes.h"
#include "envoy/router/shadow_writer.h"
#include "envoy/runtime/runtime.h"
#include "envoy/thread_local/thread_local.h"
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
  MOCK_CONST_METHOD2(finalizeResponseHeaders,
                     void(Http::HeaderMap& headers, const StreamInfo::StreamInfo& stream_info));
  MOCK_CONST_METHOD1(newPath, std::string(const Http::HeaderMap& headers));
  MOCK_CONST_METHOD2(rewritePathHeader,
                     void(Http::HeaderMap& headers, bool insert_envoy_original_path));
  MOCK_CONST_METHOD0(responseCode, Http::Code());
  MOCK_CONST_METHOD0(responseBody, const std::string&());
  MOCK_CONST_METHOD0(routeName, const std::string&());
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
  const envoy::type::FractionalPercent& additionalRequestChance() const override {
    return additional_request_chance_;
  }
  bool hedgeOnPerTryTimeout() const override { return hedge_on_per_try_timeout_; }

  uint32_t initial_requests_{};
  envoy::type::FractionalPercent additional_request_chance_{};
  bool hedge_on_per_try_timeout_{};
};

class TestRetryPolicy : public RetryPolicy {
public:
  // Router::RetryPolicy
  std::chrono::milliseconds perTryTimeout() const override { return per_try_timeout_; }
  uint32_t numRetries() const override { return num_retries_; }
  uint32_t retryOn() const override { return retry_on_; }
  MOCK_CONST_METHOD0(retryHostPredicates, std::vector<Upstream::RetryHostPredicateSharedPtr>());
  MOCK_CONST_METHOD0(retryPriority, Upstream::RetryPrioritySharedPtr());
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

  std::chrono::milliseconds per_try_timeout_{0};
  uint32_t num_retries_{};
  uint32_t retry_on_{};
  uint32_t host_selection_max_attempts_;
  std::vector<uint32_t> retriable_status_codes_;
  std::vector<Http::HeaderMatcherSharedPtr> retriable_headers_;
  std::vector<Http::HeaderMatcherSharedPtr> retriable_request_headers_;
  absl::optional<std::chrono::milliseconds> base_interval_{};
  absl::optional<std::chrono::milliseconds> max_interval_{};
};

class MockRetryState : public RetryState {
public:
  MockRetryState();
  ~MockRetryState() override;

  void expectHeadersRetry();
  void expectHedgedPerTryTimeoutRetry();
  void expectResetRetry();

  MOCK_METHOD0(enabled, bool());
  MOCK_METHOD2(shouldRetryHeaders,
               RetryStatus(const Http::HeaderMap& response_headers, DoRetryCallback callback));
  MOCK_METHOD1(wouldRetryFromHeaders, bool(const Http::HeaderMap& response_headers));
  MOCK_METHOD2(shouldRetryReset,
               RetryStatus(const Http::StreamResetReason reset_reason, DoRetryCallback callback));
  MOCK_METHOD1(shouldHedgeRetryPerTryTimeout, RetryStatus(DoRetryCallback callback));
  MOCK_METHOD1(onHostAttempted, void(Upstream::HostDescriptionConstSharedPtr));
  MOCK_METHOD1(shouldSelectAnotherHost, bool(const Upstream::Host& host));
  MOCK_METHOD2(priorityLoadForRetry,
               const Upstream::HealthyAndDegradedLoad&(const Upstream::PrioritySet&,
                                                       const Upstream::HealthyAndDegradedLoad&));
  MOCK_CONST_METHOD0(hostSelectionMaxAttempts, uint32_t());

  DoRetryCallback callback_;
};

class MockRateLimitPolicyEntry : public RateLimitPolicyEntry {
public:
  MockRateLimitPolicyEntry();
  ~MockRateLimitPolicyEntry() override;

  // Router::RateLimitPolicyEntry
  MOCK_CONST_METHOD0(stage, uint64_t());
  MOCK_CONST_METHOD0(disableKey, const std::string&());
  MOCK_CONST_METHOD5(populateDescriptors,
                     void(const RouteEntry& route,
                          std::vector<Envoy::RateLimit::Descriptor>& descriptors,
                          const std::string& local_service_cluster, const Http::HeaderMap& headers,
                          const Network::Address::Instance& remote_address));

  uint64_t stage_{};
  std::string disable_key_;
};

class MockRateLimitPolicy : public RateLimitPolicy {
public:
  MockRateLimitPolicy();
  ~MockRateLimitPolicy() override;

  // Router::RateLimitPolicy
  MOCK_CONST_METHOD1(
      getApplicableRateLimit,
      std::vector<std::reference_wrapper<const RateLimitPolicyEntry>>&(uint64_t stage));
  MOCK_CONST_METHOD0(empty, bool());

  std::vector<std::reference_wrapper<const Router::RateLimitPolicyEntry>> rate_limit_policy_entry_;
};

class TestShadowPolicy : public ShadowPolicy {
public:
  // Router::ShadowPolicy
  const std::string& cluster() const override { return cluster_; }
  const std::string& runtimeKey() const override { return runtime_key_; }
  const envoy::type::FractionalPercent& defaultValue() const override { return default_value_; }

  std::string cluster_;
  std::string runtime_key_;
  envoy::type::FractionalPercent default_value_;
};

class MockShadowWriter : public ShadowWriter {
public:
  MockShadowWriter();
  ~MockShadowWriter() override;

  // Router::ShadowWriter
  void shadow(const std::string& cluster, Http::MessagePtr&& request,
              std::chrono::milliseconds timeout) override {
    shadow_(cluster, request, timeout);
  }

  MOCK_METHOD3(shadow_, void(const std::string& cluster, Http::MessagePtr& request,
                             std::chrono::milliseconds timeout));
};

class TestVirtualCluster : public VirtualCluster {
public:
  // Router::VirtualCluster
  Stats::StatName statName() const override { return stat_name_.statName(); }

  Stats::TestSymbolTable symbol_table_;
  Stats::StatNameManagedStorage stat_name_{"fake_virtual_cluster", *symbol_table_};
};

class MockVirtualHost : public VirtualHost {
public:
  MockVirtualHost();
  ~MockVirtualHost() override;

  // Router::VirtualHost
  MOCK_CONST_METHOD0(name, const std::string&());
  MOCK_CONST_METHOD0(rateLimitPolicy, const RateLimitPolicy&());
  MOCK_CONST_METHOD0(corsPolicy, const CorsPolicy*());
  MOCK_CONST_METHOD0(routeConfig, const Config&());
  MOCK_CONST_METHOD1(perFilterConfig, const RouteSpecificFilterConfig*(const std::string&));
  MOCK_CONST_METHOD0(includeAttemptCount, bool());
  MOCK_METHOD0(retryPriority, Upstream::RetryPrioritySharedPtr());
  MOCK_METHOD0(retryHostPredicate, Upstream::RetryHostPredicateSharedPtr());
  MOCK_CONST_METHOD0(retryShadowBufferLimit, uint32_t());

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
  MOCK_CONST_METHOD3(generateHash,
                     absl::optional<uint64_t>(const Network::Address::Instance* downstream_address,
                                              const Http::HeaderMap& headers,
                                              const AddCookieCallback add_cookie));
};

class MockMetadataMatchCriteria : public MetadataMatchCriteria {
public:
  MockMetadataMatchCriteria();
  ~MockMetadataMatchCriteria() override;

  // Router::MetadataMatchCriteria
  MOCK_CONST_METHOD0(metadataMatchCriteria,
                     const std::vector<MetadataMatchCriterionConstSharedPtr>&());
  MOCK_CONST_METHOD1(mergeMatchCriteria, MetadataMatchCriteriaConstPtr(const ProtobufWkt::Struct&));
};

class MockTlsContextMatchCriteria : public TlsContextMatchCriteria {
public:
  MockTlsContextMatchCriteria();
  ~MockTlsContextMatchCriteria() override;

  // Router::MockTlsContextMatchCriteria
  MOCK_CONST_METHOD0(presented, const absl::optional<bool>&());
};

class MockPathMatchCriterion : public PathMatchCriterion {
public:
  MockPathMatchCriterion();
  ~MockPathMatchCriterion() override;

  // Router::PathMatchCriterion
  MOCK_CONST_METHOD0(matchType, PathMatchType());
  MOCK_CONST_METHOD0(matcher, const std::string&());

  PathMatchType type_;
  std::string matcher_;
};

class MockRouteEntry : public RouteEntry {
public:
  MockRouteEntry();
  ~MockRouteEntry() override;

  // Router::Config
  MOCK_CONST_METHOD0(clusterName, const std::string&());
  MOCK_CONST_METHOD0(clusterNotFoundResponseCode, Http::Code());
  MOCK_CONST_METHOD3(finalizeRequestHeaders,
                     void(Http::HeaderMap& headers, const StreamInfo::StreamInfo& stream_info,
                          bool insert_envoy_original_path));
  MOCK_CONST_METHOD2(finalizeResponseHeaders,
                     void(Http::HeaderMap& headers, const StreamInfo::StreamInfo& stream_info));
  MOCK_CONST_METHOD0(hashPolicy, const Http::HashPolicy*());
  MOCK_CONST_METHOD0(hedgePolicy, const HedgePolicy&());
  MOCK_CONST_METHOD0(metadataMatchCriteria, const Router::MetadataMatchCriteria*());
  MOCK_CONST_METHOD0(tlsContextMatchCriteria, const Router::TlsContextMatchCriteria*());
  MOCK_CONST_METHOD0(priority, Upstream::ResourcePriority());
  MOCK_CONST_METHOD0(rateLimitPolicy, const RateLimitPolicy&());
  MOCK_CONST_METHOD0(retryPolicy, const RetryPolicy&());
  MOCK_CONST_METHOD0(retryShadowBufferLimit, uint32_t());
  MOCK_CONST_METHOD0(shadowPolicy, const ShadowPolicy&());
  MOCK_CONST_METHOD0(timeout, std::chrono::milliseconds());
  MOCK_CONST_METHOD0(idleTimeout, absl::optional<std::chrono::milliseconds>());
  MOCK_CONST_METHOD0(maxGrpcTimeout, absl::optional<std::chrono::milliseconds>());
  MOCK_CONST_METHOD0(grpcTimeoutOffset, absl::optional<std::chrono::milliseconds>());
  MOCK_CONST_METHOD1(virtualCluster, const VirtualCluster*(const Http::HeaderMap& headers));
  MOCK_CONST_METHOD0(virtualHostName, const std::string&());
  MOCK_CONST_METHOD0(virtualHost, const VirtualHost&());
  MOCK_CONST_METHOD0(autoHostRewrite, bool());
  MOCK_CONST_METHOD0(opaqueConfig, const std::multimap<std::string, std::string>&());
  MOCK_CONST_METHOD0(includeVirtualHostRateLimits, bool());
  MOCK_CONST_METHOD0(corsPolicy, const CorsPolicy*());
  MOCK_CONST_METHOD0(metadata, const envoy::api::v2::core::Metadata&());
  MOCK_CONST_METHOD0(typedMetadata, const Envoy::Config::TypedMetadata&());
  MOCK_CONST_METHOD0(pathMatchCriterion, const PathMatchCriterion&());
  MOCK_CONST_METHOD1(perFilterConfig, const RouteSpecificFilterConfig*(const std::string&));
  MOCK_CONST_METHOD0(includeAttemptCount, bool());
  MOCK_CONST_METHOD0(upgradeMap, const UpgradeMap&());
  MOCK_CONST_METHOD0(internalRedirectAction, InternalRedirectAction());
  MOCK_CONST_METHOD0(routeName, const std::string&());

  std::string cluster_name_{"fake_cluster"};
  std::string route_name_{"fake_route_name"};
  std::multimap<std::string, std::string> opaque_config_;
  TestVirtualCluster virtual_cluster_;
  TestRetryPolicy retry_policy_;
  TestHedgePolicy hedge_policy_;
  testing::NiceMock<MockRateLimitPolicy> rate_limit_policy_;
  TestShadowPolicy shadow_policy_;
  testing::NiceMock<MockVirtualHost> virtual_host_;
  MockHashPolicy hash_policy_;
  MockMetadataMatchCriteria metadata_matches_criteria_;
  MockTlsContextMatchCriteria tls_context_matches_criteria_;
  TestCorsPolicy cors_policy_;
  testing::NiceMock<MockPathMatchCriterion> path_match_criterion_;
  envoy::api::v2::core::Metadata metadata_;
  UpgradeMap upgrade_map_;
};

class MockDecorator : public Decorator {
public:
  MockDecorator();
  ~MockDecorator() override;

  // Router::Decorator
  MOCK_CONST_METHOD0(getOperation, const std::string&());
  MOCK_CONST_METHOD1(apply, void(Tracing::Span& span));

  std::string operation_{"fake_operation"};
};

class MockRouteTracing : public RouteTracing {
public:
  MockRouteTracing();
  ~MockRouteTracing() override;

  // Router::RouteTracing
  MOCK_CONST_METHOD0(getClientSampling, const envoy::type::FractionalPercent&());
  MOCK_CONST_METHOD0(getRandomSampling, const envoy::type::FractionalPercent&());
  MOCK_CONST_METHOD0(getOverallSampling, const envoy::type::FractionalPercent&());
};

class MockRoute : public Route {
public:
  MockRoute();
  ~MockRoute() override;

  // Router::Route
  MOCK_CONST_METHOD0(directResponseEntry, const DirectResponseEntry*());
  MOCK_CONST_METHOD0(routeEntry, const RouteEntry*());
  MOCK_CONST_METHOD0(decorator, const Decorator*());
  MOCK_CONST_METHOD0(tracingConfig, const RouteTracing*());
  MOCK_CONST_METHOD1(perFilterConfig, const RouteSpecificFilterConfig*(const std::string&));

  testing::NiceMock<MockRouteEntry> route_entry_;
  testing::NiceMock<MockDecorator> decorator_;
  testing::NiceMock<MockRouteTracing> route_tracing_;
};

class MockConfig : public Config {
public:
  MockConfig();
  ~MockConfig() override;

  // Router::Config
  MOCK_CONST_METHOD3(route, RouteConstSharedPtr(const Http::HeaderMap&,
                                                const Envoy::StreamInfo::StreamInfo&,
                                                uint64_t random_value));
  MOCK_CONST_METHOD0(internalOnlyHeaders, const std::list<Http::LowerCaseString>&());
  MOCK_CONST_METHOD0(name, const std::string&());
  MOCK_CONST_METHOD0(usesVhds, bool());
  MOCK_CONST_METHOD0(mostSpecificHeaderMutationsWins, bool());

  std::shared_ptr<MockRoute> route_;
  std::list<Http::LowerCaseString> internal_only_headers_;
  std::string name_{"fake_config"};
};

class MockRouteConfigProvider : public RouteConfigProvider {
public:
  MockRouteConfigProvider();
  ~MockRouteConfigProvider() override;

  MOCK_METHOD0(config, ConfigConstSharedPtr());
  MOCK_CONST_METHOD0(configInfo, absl::optional<ConfigInfo>());
  MOCK_CONST_METHOD0(lastUpdated, SystemTime());
  MOCK_METHOD0(onConfigUpdate, void());
  MOCK_CONST_METHOD1(validateConfig, void(const envoy::api::v2::RouteConfiguration&));

  std::shared_ptr<NiceMock<MockConfig>> route_config_{new NiceMock<MockConfig>()};
};

class MockRouteConfigProviderManager : public RouteConfigProviderManager {
public:
  MockRouteConfigProviderManager();
  ~MockRouteConfigProviderManager() override;

  MOCK_METHOD4(createRdsRouteConfigProvider,
               RouteConfigProviderPtr(
                   const envoy::config::filter::network::http_connection_manager::v2::Rds& rds,
                   Server::Configuration::FactoryContext& factory_context,
                   const std::string& stat_prefix, Init::Manager& init_manager));
  MOCK_METHOD2(createStaticRouteConfigProvider,
               RouteConfigProviderPtr(const envoy::api::v2::RouteConfiguration& route_config,
                                      Server::Configuration::FactoryContext& factory_context));
};

class MockScopedConfig : public ScopedConfig {
public:
  MockScopedConfig();
  ~MockScopedConfig() override;

  MOCK_CONST_METHOD1(getRouteConfig, ConfigConstSharedPtr(const Http::HeaderMap& headers));

  std::shared_ptr<MockConfig> route_config_{new NiceMock<MockConfig>()};
};

class MockScopedRouteConfigProvider : public Envoy::Config::ConfigProvider {
public:
  MockScopedRouteConfigProvider();
  ~MockScopedRouteConfigProvider() override;

  // Config::ConfigProvider
  MOCK_CONST_METHOD0(lastUpdated, SystemTime());
  MOCK_CONST_METHOD0(getConfigProto, Protobuf::Message*());
  MOCK_CONST_METHOD0(getConfigProtos, Envoy::Config::ConfigProvider::ConfigProtoVector());
  MOCK_CONST_METHOD0(getConfig, ConfigConstSharedPtr());
  MOCK_CONST_METHOD0(apiType, ApiType());

  std::shared_ptr<MockScopedConfig> config_;
};

} // namespace Router
} // namespace Envoy
