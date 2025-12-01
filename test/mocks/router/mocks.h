#pragma once

#include <chrono>
#include <cstdint>
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
#include "envoy/http/stateful_session.h"
#include "envoy/local_info/local_info.h"
#include "envoy/router/cluster_specifier_plugin.h"
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

#include "source/common/router/upstream_to_downstream_impl_base.h"
#include "source/common/stats/symbol_table.h"

#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/global.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Router {
using ::testing::NiceMock;

struct MockRouteMetadataObj : public Envoy::Config::TypedMetadata::Object {};
class MockRouteMetadata : public Envoy::Config::TypedMetadata {
public:
  const Envoy::Config::TypedMetadata::Object* getData(const std::string&) const override {
    return &object_;
  }

private:
  MockRouteMetadataObj object_;
};

class MockDirectResponseEntry : public DirectResponseEntry {
public:
  MockDirectResponseEntry();
  ~MockDirectResponseEntry() override;

  // DirectResponseEntry
  MOCK_METHOD(void, finalizeResponseHeaders,
              (Http::ResponseHeaderMap & headers, const Formatter::Context& context,
               const StreamInfo::StreamInfo& stream_info),
              (const));
  MOCK_METHOD(Http::HeaderTransforms, responseHeaderTransforms,
              (const StreamInfo::StreamInfo& stream_info, bool do_formatting), (const));
  MOCK_METHOD(std::string, newUri, (const Http::RequestHeaderMap& headers), (const));
  MOCK_METHOD(void, rewritePathHeader,
              (Http::RequestHeaderMap & headers, bool insert_envoy_original_path), (const));
  MOCK_METHOD(Http::Code, responseCode, (), (const));
  MOCK_METHOD(absl::string_view, formatBody,
              (const Http::RequestHeaderMap& request_headers,
               const Http::ResponseHeaderMap& response_headers,
               const StreamInfo::StreamInfo& stream_info, std::string& body_out),
              (const));
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
  const absl::optional<bool>& allowPrivateNetworkAccess() const override {
    return allow_private_network_access_;
  };
  bool enabled() const override { return enabled_; };
  bool shadowEnabled() const override { return shadow_enabled_; };
  const absl::optional<bool>& forwardNotMatchingPreflights() const override {
    return forward_not_matching_preflights_;
  };

  std::vector<Matchers::StringMatcherPtr> allow_origins_;
  std::string allow_methods_;
  std::string allow_headers_;
  std::string expose_headers_;
  std::string max_age_;
  absl::optional<bool> allow_credentials_;
  absl::optional<bool> allow_private_network_access_;
  bool enabled_{};
  bool shadow_enabled_{};
  absl::optional<bool> forward_not_matching_preflights_;
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
  envoy::type::v3::FractionalPercent additional_request_chance_;
  bool hedge_on_per_try_timeout_{};
};

class TestRetryPolicy : public RetryPolicy {
public:
  static std::shared_ptr<TestRetryPolicy> create() { return std::make_shared<TestRetryPolicy>(); }
  static std::shared_ptr<NiceMock<TestRetryPolicy>> createMock() {
    return std::make_shared<NiceMock<TestRetryPolicy>>();
  }
  TestRetryPolicy();
  ~TestRetryPolicy() override;

  // Router::RetryPolicy
  std::chrono::milliseconds perTryTimeout() const override { return per_try_timeout_; }
  std::chrono::milliseconds perTryIdleTimeout() const override { return per_try_idle_timeout_; }
  uint32_t numRetries() const override { return num_retries_; }
  uint32_t retryOn() const override { return retry_on_; }
  MOCK_METHOD(std::vector<Upstream::RetryHostPredicateSharedPtr>, retryHostPredicates, (), (const));
  MOCK_METHOD(Upstream::RetryPrioritySharedPtr, retryPriority, (), (const));
  absl::Span<const Upstream::RetryOptionsPredicateConstSharedPtr>
  retryOptionsPredicates() const override {
    return retry_options_predicates_;
  }
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
  std::chrono::milliseconds resetMaxInterval() const override { return reset_max_interval_; }
  const std::vector<ResetHeaderParserSharedPtr>& resetHeaders() const override {
    return reset_headers_;
  }

  std::chrono::milliseconds per_try_timeout_{0};
  std::chrono::milliseconds per_try_idle_timeout_{0};
  uint32_t num_retries_{};
  uint32_t retry_on_{};
  uint32_t host_selection_max_attempts_{0};
  std::vector<uint32_t> retriable_status_codes_;
  std::vector<Http::HeaderMatcherSharedPtr> retriable_headers_;
  std::vector<Http::HeaderMatcherSharedPtr> retriable_request_headers_;
  absl::optional<std::chrono::milliseconds> base_interval_;
  absl::optional<std::chrono::milliseconds> max_interval_;
  std::vector<ResetHeaderParserSharedPtr> reset_headers_;
  std::chrono::milliseconds reset_max_interval_{300000};
  std::vector<Upstream::RetryOptionsPredicateConstSharedPtr> retry_options_predicates_;
};

class MockInternalRedirectPolicy : public InternalRedirectPolicy {
public:
  MockInternalRedirectPolicy();
  MOCK_METHOD(bool, enabled, (), (const));
  MOCK_METHOD(bool, shouldRedirectForResponseCode, (const Http::Code& response_code), (const));
  MOCK_METHOD(std::vector<InternalRedirectPredicateSharedPtr>, predicates, (), (const));
  MOCK_METHOD(uint32_t, maxInternalRedirects, (), (const));
  MOCK_METHOD(bool, isCrossSchemeRedirectAllowed, (), (const));
  MOCK_METHOD(const std::vector<Http::LowerCaseString>&, responseHeadersToCopy, (), (const));
};

class MockInternalRedirectPredicate : public InternalRedirectPredicate {
public:
  MOCK_METHOD(bool, acceptTargetRoute, (StreamInfo::FilterState&, absl::string_view, bool, bool));
  MOCK_METHOD(absl::string_view, name, (), (const));
};

class MockPathRewriter : public PathRewriter {
public:
  MOCK_METHOD(absl::string_view, name, (), (const));
  MOCK_METHOD(absl::StatusOr<std::string>, rewritePath,
              (absl::string_view path, absl::string_view rewrite_pattern), (const));
  MOCK_METHOD(absl::string_view, uriTemplate, (), (const));
  MOCK_METHOD(absl::Status, isCompatiblePathMatcher, (PathMatcherSharedPtr path_matcher), (const));
};

class MockPathMatcher : public PathMatcher {
public:
  MOCK_METHOD(absl::string_view, name, (), (const));
  MOCK_METHOD(bool, match, (absl::string_view path), (const));
  MOCK_METHOD(absl::string_view, uriTemplate, (), (const));
};

class MockRetryState : public RetryState {
public:
  MockRetryState();
  ~MockRetryState() override;

  void expectHeadersRetry();
  void expectHedgedPerTryTimeoutRetry();
  void expectResetRetry();

  MOCK_METHOD(bool, enabled, ());
  MOCK_METHOD(absl::optional<std::chrono::milliseconds>, parseResetInterval,
              (const Http::ResponseHeaderMap& response_headers), (const));
  MOCK_METHOD(RetryStatus, shouldRetryHeaders,
              (const Http::ResponseHeaderMap& response_headers,
               const Http::RequestHeaderMap& original_request, DoRetryHeaderCallback callback));
  MOCK_METHOD(RetryState::RetryDecision, wouldRetryFromHeaders,
              (const Http::ResponseHeaderMap& response_headers,
               const Http::RequestHeaderMap& original_request, bool& retry_as_early_data));
  MOCK_METHOD(RetryStatus, shouldRetryReset,
              (const Http::StreamResetReason reset_reason, Http3Used alternate_protocol_used,
               DoRetryResetCallback callback, bool upstream_request_started));
  MOCK_METHOD(RetryStatus, shouldHedgeRetryPerTryTimeout, (DoRetryCallback callback));
  MOCK_METHOD(void, onHostAttempted, (Upstream::HostDescriptionConstSharedPtr));
  MOCK_METHOD(bool, shouldSelectAnotherHost, (const Upstream::Host& host));
  MOCK_METHOD(const Upstream::HealthyAndDegradedLoad&, priorityLoadForRetry,
              (const Upstream::PrioritySet&, const Upstream::HealthyAndDegradedLoad&,
               const Upstream::RetryPriority::PriorityMappingFunc&));
  MOCK_METHOD(uint32_t, hostSelectionMaxAttempts, (), (const));
  MOCK_METHOD(bool, wouldRetryFromRetriableStatusCode, (Http::Code code), (const));

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
              (std::vector<Envoy::RateLimit::Descriptor> & descriptors,
               const std::string& local_service_cluster, const Http::RequestHeaderMap& headers,
               const StreamInfo::StreamInfo& info),
              (const));
  MOCK_METHOD(void, populateLocalDescriptors,
              (std::vector<Envoy::RateLimit::LocalDescriptor> & descriptors,
               const std::string& local_service_cluster, const Http::RequestHeaderMap& headers,
               const StreamInfo::StreamInfo& info),
              (const));
  MOCK_METHOD(bool, applyOnStreamDone, (), (const));

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

  Http::AsyncClient::OngoingRequest*
  streamingShadow(const std::string& cluster, Http::RequestHeaderMapPtr&& request,
                  const Http::AsyncClient::RequestOptions& options) override {
    return streamingShadow_(cluster, request, options);
  }
  MOCK_METHOD(Http::AsyncClient::OngoingRequest*, streamingShadow_,
              (const std::string& cluster, Http::RequestHeaderMapPtr& request,
               const Http::AsyncClient::RequestOptions& options));
};

class TestVirtualCluster : public VirtualCluster {
public:
  // Router::VirtualCluster
  const absl::optional<std::string>& name() const override { return name_; }
  Stats::StatName statName() const override { return stat_name_.statName(); }
  VirtualClusterStats& stats() const override { return stats_; }

  const absl::optional<std::string> name_ = "fake_virtual_cluster";
  Stats::TestUtil::TestSymbolTable symbol_table_;
  Stats::StatNameManagedStorage stat_name_{"fake_virtual_cluster", *symbol_table_};
  Stats::IsolatedStoreImpl stats_store_;
  VirtualClusterStatNames stat_names_{stats_store_.symbolTable()};
  mutable VirtualClusterStats stats_{generateStats(*stats_store_.rootScope(), stat_names_)};
};

class MockVirtualHost : public VirtualHost {
public:
  MockVirtualHost();
  ~MockVirtualHost() override;

  // Router::VirtualHost
  MOCK_METHOD(const std::string&, name, (), (const));
  MOCK_METHOD(const RateLimitPolicy&, rateLimitPolicy, (), (const));
  MOCK_METHOD(const CorsPolicy*, corsPolicy, (), (const));
  MOCK_METHOD(const CommonConfig&, routeConfig, (), (const));
  MOCK_METHOD(const RouteSpecificFilterConfig*, mostSpecificPerFilterConfig, (absl::string_view),
              (const));
  MOCK_METHOD(bool, includeAttemptCountInRequest, (), (const));
  MOCK_METHOD(bool, includeAttemptCountInResponse, (), (const));
  MOCK_METHOD(bool, includeIsTimeoutRetryHeader, (), (const));
  MOCK_METHOD(Upstream::RetryPrioritySharedPtr, retryPriority, ());
  MOCK_METHOD(Upstream::RetryHostPredicateSharedPtr, retryHostPredicate, ());
  MOCK_METHOD(uint64_t, requestBodyBufferLimit, (), (const));
  MOCK_METHOD(RouteSpecificFilterConfigs, perFilterConfigs, (absl::string_view), (const));
  MOCK_METHOD(const envoy::config::core::v3::Metadata&, metadata, (), (const));
  MOCK_METHOD(const Envoy::Config::TypedMetadata&, typedMetadata, (), (const));
  MOCK_METHOD(const VirtualCluster*, virtualCluster, (const Http::HeaderMap& headers), (const));

  Stats::StatName statName() const override {
    stat_name_ = std::make_unique<Stats::StatNameManagedStorage>(name(), *symbol_table_);
    return stat_name_->statName();
  }

  mutable Stats::TestUtil::TestSymbolTable symbol_table_;
  std::string name_{"fake_vhost"};
  mutable std::unique_ptr<Stats::StatNameManagedStorage> stat_name_;
  testing::NiceMock<MockRateLimitPolicy> rate_limit_policy_;
  TestCorsPolicy cors_policy_;
  envoy::config::core::v3::Metadata metadata_;
  MockRouteMetadata typed_metadata_;
  TestVirtualCluster virtual_cluster_;
};

class MockHashPolicy : public Http::HashPolicy {
public:
  MockHashPolicy();
  ~MockHashPolicy() override;

  // Http::HashPolicy
  MOCK_METHOD(absl::optional<uint64_t>, generateHash,
              (OptRef<const Http::RequestHeaderMap> headers,
               OptRef<const StreamInfo::StreamInfo> info,
               Http::HashPolicy::AddCookieCallback add_cookie),
              (const));
};

class MockMetadataMatchCriteria : public MetadataMatchCriteria {
public:
  MockMetadataMatchCriteria();
  ~MockMetadataMatchCriteria() override;

  // Router::MetadataMatchCriteria
  MOCK_METHOD(const std::vector<MetadataMatchCriterionConstSharedPtr>&, metadataMatchCriteria, (),
              (const));
  MOCK_METHOD(MetadataMatchCriteriaConstPtr, mergeMatchCriteria, (const Protobuf::Struct&),
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

class MockEarlyDataPolicy : public EarlyDataPolicy {
public:
  MOCK_METHOD(bool, allowsEarlyDataForRequest, (const Http::RequestHeaderMap& request_headers),
              (const));
};

class MockPathMatchCriterion : public PathMatchCriterion {
public:
  MockPathMatchCriterion();
  ~MockPathMatchCriterion() override;

  // Router::PathMatchCriterion
  MOCK_METHOD(PathMatchType, matchType, (), (const));
  MOCK_METHOD(const std::string&, matcher, (), (const));

  PathMatchType type_{};
  std::string matcher_;
};

class MockRouteEntry : public RouteEntry {
public:
  MockRouteEntry();
  ~MockRouteEntry() override;

  // Router::RouteEntry
  MOCK_METHOD(const std::string&, clusterName, (), (const));
  MOCK_METHOD(Http::Code, clusterNotFoundResponseCode, (), (const));
  MOCK_METHOD(void, finalizeRequestHeaders,
              (Http::RequestHeaderMap & headers, const Formatter::Context& context,
               const StreamInfo::StreamInfo& stream_info, bool insert_envoy_original_path),
              (const));
  MOCK_METHOD(Http::HeaderTransforms, requestHeaderTransforms,
              (const StreamInfo::StreamInfo& stream_info, bool do_formatting), (const));
  MOCK_METHOD(void, finalizeResponseHeaders,
              (Http::ResponseHeaderMap & headers, const Formatter::Context& context,
               const StreamInfo::StreamInfo& stream_info),
              (const));
  MOCK_METHOD(Http::HeaderTransforms, responseHeaderTransforms,
              (const StreamInfo::StreamInfo& stream_info, bool do_formatting), (const));
  MOCK_METHOD(const Http::HashPolicy*, hashPolicy, (), (const));
  MOCK_METHOD(const HedgePolicy&, hedgePolicy, (), (const));
  MOCK_METHOD(const Router::MetadataMatchCriteria*, metadataMatchCriteria, (), (const));
  MOCK_METHOD(const Router::TlsContextMatchCriteria*, tlsContextMatchCriteria, (), (const));
  MOCK_METHOD(Upstream::ResourcePriority, priority, (), (const));
  MOCK_METHOD(const RateLimitPolicy&, rateLimitPolicy, (), (const));
  MOCK_METHOD(const RetryPolicyConstSharedPtr&, retryPolicy, (), (const));
  MOCK_METHOD(const InternalRedirectPolicy&, internalRedirectPolicy, (), (const));
  MOCK_METHOD(const PathMatcherSharedPtr&, pathMatcher, (), (const));
  MOCK_METHOD(const PathRewriterSharedPtr&, pathRewriter, (), (const));
  MOCK_METHOD(uint64_t, requestBodyBufferLimit, (), (const));
  MOCK_METHOD(const std::vector<ShadowPolicyPtr>&, shadowPolicies, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, timeout, (), (const));
  MOCK_METHOD(absl::optional<std::chrono::milliseconds>, idleTimeout, (), (const));
  MOCK_METHOD(absl::optional<std::chrono::milliseconds>, flushTimeout, (), (const));
  MOCK_METHOD(bool, usingNewTimeouts, (), (const));
  MOCK_METHOD(absl::optional<std::chrono::milliseconds>, maxStreamDuration, (), (const));
  MOCK_METHOD(absl::optional<std::chrono::milliseconds>, grpcTimeoutHeaderMax, (), (const));
  MOCK_METHOD(absl::optional<std::chrono::milliseconds>, grpcTimeoutHeaderOffset, (), (const));
  MOCK_METHOD(absl::optional<std::chrono::milliseconds>, maxGrpcTimeout, (), (const));
  MOCK_METHOD(absl::optional<std::chrono::milliseconds>, grpcTimeoutOffset, (), (const));
  MOCK_METHOD(bool, autoHostRewrite, (), (const));
  MOCK_METHOD(bool, appendXfh, (), (const));
  MOCK_METHOD((const std::multimap<std::string, std::string>&), opaqueConfig, (), (const));
  MOCK_METHOD(bool, includeVirtualHostRateLimits, (), (const));
  MOCK_METHOD(const CorsPolicy*, corsPolicy, (), (const));
  MOCK_METHOD(std::string, currentUrlPathAfterRewrite,
              (const Http::RequestHeaderMap&, const Formatter::Context&,
               const StreamInfo::StreamInfo&),
              (const));
  MOCK_METHOD(const PathMatchCriterion&, pathMatchCriterion, (), (const));
  MOCK_METHOD(bool, includeAttemptCountInRequest, (), (const));
  MOCK_METHOD(bool, includeAttemptCountInResponse, (), (const));
  MOCK_METHOD(const ConnectConfigOptRef, connectConfig, (), (const));
  MOCK_METHOD(const UpgradeMap&, upgradeMap, (), (const));
  MOCK_METHOD(const EarlyDataPolicy&, earlyDataPolicy, (), (const));
  MOCK_METHOD(const RouteStatsContextOptRef, routeStatsContext, (), (const));
  MOCK_METHOD(void, refreshRouteCluster,
              (const Http::RequestHeaderMap&, const StreamInfo::StreamInfo&), (const));

  std::string cluster_name_{"fake_cluster"};
  std::multimap<std::string, std::string> opaque_config_;
  std::shared_ptr<TestRetryPolicy> retry_policy_ = TestRetryPolicy::create();
  RetryPolicyConstSharedPtr base_retry_policy_ = retry_policy_;
  testing::NiceMock<MockInternalRedirectPolicy> internal_redirect_policy_;
  PathMatcherSharedPtr path_matcher_;
  PathRewriterSharedPtr path_rewriter_;
  TestHedgePolicy hedge_policy_;
  testing::NiceMock<MockRateLimitPolicy> rate_limit_policy_;
  std::vector<ShadowPolicyPtr> shadow_policies_;
  MockHashPolicy hash_policy_;
  MockMetadataMatchCriteria metadata_matches_criteria_;
  MockTlsContextMatchCriteria tls_context_matches_criteria_;
  TestCorsPolicy cors_policy_;
  testing::NiceMock<MockPathMatchCriterion> path_match_criterion_;
  UpgradeMap upgrade_map_;
  absl::optional<ConnectConfig> connect_config_;
  testing::NiceMock<MockEarlyDataPolicy> early_data_policy_;
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

  envoy::type::v3::FractionalPercent client_sampling_;
  envoy::type::v3::FractionalPercent random_sampling_;
  envoy::type::v3::FractionalPercent overall_sampling_;
  Tracing::CustomTagMap custom_tags_;
};

class MockRoute : public RouteEntryAndRoute {
public:
  MockRoute();
  ~MockRoute() override;

  // Router::Route
  MOCK_METHOD(const DirectResponseEntry*, directResponseEntry, (), (const));
  MOCK_METHOD(const RouteEntry*, routeEntry, (), (const));
  MOCK_METHOD(const Decorator*, decorator, (), (const));
  MOCK_METHOD(const RouteTracing*, tracingConfig, (), (const));
  MOCK_METHOD(absl::optional<bool>, filterDisabled, (absl::string_view), (const));
  MOCK_METHOD(const RouteSpecificFilterConfig*, perFilterConfig, (absl::string_view), (const));
  MOCK_METHOD(const RouteSpecificFilterConfig*, mostSpecificPerFilterConfig, (absl::string_view),
              (const));
  MOCK_METHOD(RouteSpecificFilterConfigs, perFilterConfigs, (absl::string_view), (const));
  MOCK_METHOD(const envoy::config::core::v3::Metadata&, metadata, (), (const));
  MOCK_METHOD(const Envoy::Config::TypedMetadata&, typedMetadata, (), (const));
  MOCK_METHOD(const std::string&, routeName, (), (const));
  MOCK_METHOD(const VirtualHostConstSharedPtr&, virtualHost, (), (const));

  // Router::RouteEntry
  MOCK_METHOD(const std::string&, clusterName, (), (const));
  MOCK_METHOD(Http::Code, clusterNotFoundResponseCode, (), (const));
  MOCK_METHOD(void, finalizeRequestHeaders,
              (Http::RequestHeaderMap & headers, const Formatter::Context& context,
               const StreamInfo::StreamInfo& stream_info, bool insert_envoy_original_path),
              (const));
  MOCK_METHOD(Http::HeaderTransforms, requestHeaderTransforms,
              (const StreamInfo::StreamInfo& stream_info, bool do_formatting), (const));
  MOCK_METHOD(void, finalizeResponseHeaders,
              (Http::ResponseHeaderMap & headers, const Formatter::Context& context,
               const StreamInfo::StreamInfo& stream_info),
              (const));
  MOCK_METHOD(Http::HeaderTransforms, responseHeaderTransforms,
              (const StreamInfo::StreamInfo& stream_info, bool do_formatting), (const));
  MOCK_METHOD(const Http::HashPolicy*, hashPolicy, (), (const));
  MOCK_METHOD(const HedgePolicy&, hedgePolicy, (), (const));
  MOCK_METHOD(const Router::MetadataMatchCriteria*, metadataMatchCriteria, (), (const));
  MOCK_METHOD(const Router::TlsContextMatchCriteria*, tlsContextMatchCriteria, (), (const));
  MOCK_METHOD(Upstream::ResourcePriority, priority, (), (const));
  MOCK_METHOD(const RateLimitPolicy&, rateLimitPolicy, (), (const));
  MOCK_METHOD(const RetryPolicyConstSharedPtr&, retryPolicy, (), (const));
  MOCK_METHOD(const InternalRedirectPolicy&, internalRedirectPolicy, (), (const));
  MOCK_METHOD(const PathMatcherSharedPtr&, pathMatcher, (), (const));
  MOCK_METHOD(const PathRewriterSharedPtr&, pathRewriter, (), (const));
  MOCK_METHOD(uint64_t, requestBodyBufferLimit, (), (const));
  MOCK_METHOD(const std::vector<ShadowPolicyPtr>&, shadowPolicies, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, timeout, (), (const));
  MOCK_METHOD(absl::optional<std::chrono::milliseconds>, idleTimeout, (), (const));
  MOCK_METHOD(absl::optional<std::chrono::milliseconds>, flushTimeout, (), (const));
  MOCK_METHOD(bool, usingNewTimeouts, (), (const));
  MOCK_METHOD(absl::optional<std::chrono::milliseconds>, maxStreamDuration, (), (const));
  MOCK_METHOD(absl::optional<std::chrono::milliseconds>, grpcTimeoutHeaderMax, (), (const));
  MOCK_METHOD(absl::optional<std::chrono::milliseconds>, grpcTimeoutHeaderOffset, (), (const));
  MOCK_METHOD(absl::optional<std::chrono::milliseconds>, maxGrpcTimeout, (), (const));
  MOCK_METHOD(absl::optional<std::chrono::milliseconds>, grpcTimeoutOffset, (), (const));
  MOCK_METHOD(bool, autoHostRewrite, (), (const));
  MOCK_METHOD(bool, appendXfh, (), (const));
  MOCK_METHOD((const std::multimap<std::string, std::string>&), opaqueConfig, (), (const));
  MOCK_METHOD(bool, includeVirtualHostRateLimits, (), (const));
  MOCK_METHOD(const CorsPolicy*, corsPolicy, (), (const));
  MOCK_METHOD(std::string, currentUrlPathAfterRewrite,
              (const Http::RequestHeaderMap&, const Formatter::Context&,
               const StreamInfo::StreamInfo&),
              (const));
  MOCK_METHOD(const PathMatchCriterion&, pathMatchCriterion, (), (const));
  MOCK_METHOD(bool, includeAttemptCountInRequest, (), (const));
  MOCK_METHOD(bool, includeAttemptCountInResponse, (), (const));
  MOCK_METHOD(const ConnectConfigOptRef, connectConfig, (), (const));
  MOCK_METHOD(const UpgradeMap&, upgradeMap, (), (const));
  MOCK_METHOD(const EarlyDataPolicy&, earlyDataPolicy, (), (const));
  MOCK_METHOD(const RouteStatsContextOptRef, routeStatsContext, (), (const));
  MOCK_METHOD(void, refreshRouteCluster,
              (const Http::RequestHeaderMap&, const StreamInfo::StreamInfo&), (const));

  testing::NiceMock<MockRouteEntry> route_entry_;
  testing::NiceMock<MockDecorator> decorator_;
  testing::NiceMock<MockRouteTracing> route_tracing_;
  envoy::config::core::v3::Metadata metadata_;
  MockRouteMetadata typed_metadata_;
  std::string route_name_{"fake_route_name"};
  std::shared_ptr<testing::NiceMock<MockVirtualHost>> virtual_host_ =
      std::make_shared<testing::NiceMock<MockVirtualHost>>();
  // Same with virtual_host_ but this could be returned as VirtualHostConstSharedPtr reference.
  VirtualHostConstSharedPtr virtual_host_copy_ = virtual_host_;
};

class MockConfig : public Config {
public:
  MockConfig();
  ~MockConfig() override;

  // Router::Config
  MOCK_METHOD(VirtualHostRoute, route,
              (const Http::RequestHeaderMap&, const Envoy::StreamInfo::StreamInfo&,
               uint64_t random_value),
              (const));
  MOCK_METHOD(VirtualHostRoute, route,
              (const RouteCallback& cb, const Http::RequestHeaderMap&,
               const Envoy::StreamInfo::StreamInfo&, uint64_t random_value),
              (const));

  MOCK_METHOD(const std::vector<Http::LowerCaseString>&, internalOnlyHeaders, (), (const));
  MOCK_METHOD(const std::string&, name, (), (const));
  MOCK_METHOD(bool, usesVhds, (), (const));
  MOCK_METHOD(bool, mostSpecificHeaderMutationsWins, (), (const));
  MOCK_METHOD(uint32_t, maxDirectResponseBodySizeBytes, (), (const));
  MOCK_METHOD(const envoy::config::core::v3::Metadata&, metadata, (), (const));
  MOCK_METHOD(const Envoy::Config::TypedMetadata&, typedMetadata, (), (const));

  std::shared_ptr<MockRoute> route_;
  std::vector<Http::LowerCaseString> internal_only_headers_;
  std::string name_{"fake_config"};
  envoy::config::core::v3::Metadata metadata_;
  MockRouteMetadata typed_metadata_;
};

class MockRouteConfigProvider : public RouteConfigProvider {
public:
  MockRouteConfigProvider();
  ~MockRouteConfigProvider() override;

  MOCK_METHOD(Rds::ConfigConstSharedPtr, config, (), (const));
  MOCK_METHOD(const absl::optional<ConfigInfo>&, configInfo, (), (const));
  MOCK_METHOD(SystemTime, lastUpdated, (), (const));
  MOCK_METHOD(absl::Status, onConfigUpdate, ());
  MOCK_METHOD(ConfigConstSharedPtr, configCast, (), (const));
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

  MOCK_METHOD(ConfigConstSharedPtr, getRouteConfig, (const ScopeKeyPtr& scope_key), (const));

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

class MockScopeKeyBuilder : public ScopeKeyBuilder {
public:
  MockScopeKeyBuilder();
  ~MockScopeKeyBuilder() override;

  MOCK_METHOD(ScopeKeyPtr, computeScopeKey, (const Http::HeaderMap&), (const));
};

class MockGenericConnPool : public GenericConnPool {
public:
  MockGenericConnPool();
  ~MockGenericConnPool() override;

  MOCK_METHOD(void, newStream, (GenericConnectionPoolCallbacks * request));
  MOCK_METHOD(bool, cancelAnyPendingStream, ());
  MOCK_METHOD(absl::optional<Http::Protocol>, protocol, (), (const));
  MOCK_METHOD(bool, initialize,
              (Upstream::ClusterManager&, const RouteEntry&, Http::Protocol,
               Upstream::LoadBalancerContext*));
  MOCK_METHOD(Upstream::HostDescriptionConstSharedPtr, host, (), (const));
  MOCK_METHOD(bool, valid, (), (const));

  std::shared_ptr<NiceMock<Upstream::MockHostDescription>> host_{
      new NiceMock<Upstream::MockHostDescription>()};
};

class MockUpstreamToDownstream : public UpstreamToDownstreamImplBase {
public:
  MOCK_METHOD(const Route&, route, (), (const));
  MOCK_METHOD(OptRef<const Network::Connection>, connection, (), (const));

  MOCK_METHOD(void, decodeData, (Buffer::Instance&, bool));
  MOCK_METHOD(void, decodeMetadata, (Http::MetadataMapPtr&&));
  MOCK_METHOD(void, decode1xxHeaders, (Http::ResponseHeaderMapPtr&&));
  MOCK_METHOD(void, decodeHeaders, (Http::ResponseHeaderMapPtr&&, bool));
  MOCK_METHOD(void, decodeTrailers, (Http::ResponseTrailerMapPtr&&));
  MOCK_METHOD(void, dumpState, (std::ostream&, int), (const));

  MOCK_METHOD(void, onResetStream, (Http::StreamResetReason, absl::string_view));
  MOCK_METHOD(void, onAboveWriteBufferHighWatermark, ());
  MOCK_METHOD(void, onBelowWriteBufferLowWatermark, ());
  MOCK_METHOD(const Http::ConnectionPool::Instance::StreamOptions&, upstreamStreamOptions, (),
              (const));
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
               const Network::ConnectionInfoProvider& info_provider, StreamInfo::StreamInfo& info,
               absl::optional<Http::Protocol> protocol));
  MOCK_METHOD(UpstreamToDownstream&, upstreamToDownstream, ());

  NiceMock<MockUpstreamToDownstream> upstream_to_downstream_;
};

class MockClusterSpecifierPlugin : public ClusterSpecifierPlugin {
public:
  MockClusterSpecifierPlugin();

  MOCK_METHOD(RouteConstSharedPtr, route,
              (RouteEntryAndRouteConstSharedPtr parent, const Http::RequestHeaderMap& headers,
               const StreamInfo::StreamInfo& stream_info, uint64_t random),
              (const));
};

class MockClusterSpecifierPluginFactoryConfig : public ClusterSpecifierPluginFactoryConfig {
public:
  MockClusterSpecifierPluginFactoryConfig();
  MOCK_METHOD(ClusterSpecifierPluginSharedPtr, createClusterSpecifierPlugin,
              (const Protobuf::Message& config,
               Server::Configuration::ServerFactoryContext& context));

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }

  std::string name() const override { return "envoy.router.cluster_specifier_plugin.mock"; }
};

} // namespace Router
} // namespace Envoy
