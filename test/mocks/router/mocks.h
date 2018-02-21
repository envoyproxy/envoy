#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "envoy/event/dispatcher.h"
#include "envoy/init/init.h"
#include "envoy/json/json_object.h"
#include "envoy/local_info/local_info.h"
#include "envoy/router/rds.h"
#include "envoy/router/route_config_provider_manager.h"
#include "envoy/router/router.h"
#include "envoy/router/router_ratelimit.h"
#include "envoy/router/shadow_writer.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Router {

class MockDirectResponseEntry : public DirectResponseEntry {
public:
  MockDirectResponseEntry();
  ~MockDirectResponseEntry();

  // DirectResponseEntry
  MOCK_CONST_METHOD2(finalizeResponseHeaders,
                     void(Http::HeaderMap& headers, const RequestInfo::RequestInfo& request_info));
  MOCK_CONST_METHOD1(newPath, std::string(const Http::HeaderMap& headers));
  MOCK_CONST_METHOD1(rewritePathHeader, void(Http::HeaderMap& headers));
  MOCK_CONST_METHOD0(responseCode, Http::Code());
  MOCK_CONST_METHOD0(responseBody, const std::string&());
};

class TestCorsPolicy : public CorsPolicy {
public:
  // Router::CorsPolicy
  const std::list<std::string>& allowOrigins() const override { return allow_origin_; };
  const std::string& allowMethods() const override { return allow_methods_; };
  const std::string& allowHeaders() const override { return allow_headers_; };
  const std::string& exposeHeaders() const override { return expose_headers_; };
  const std::string& maxAge() const override { return max_age_; };
  const Optional<bool>& allowCredentials() const override { return allow_credentials_; };
  bool enabled() const override { return enabled_; };

  std::list<std::string> allow_origin_{};
  std::string allow_methods_{};
  std::string allow_headers_{};
  std::string expose_headers_{};
  std::string max_age_{};
  Optional<bool> allow_credentials_{};
  bool enabled_{false};
};

class TestRetryPolicy : public RetryPolicy {
public:
  // Router::RetryPolicy
  std::chrono::milliseconds perTryTimeout() const override { return per_try_timeout_; }
  uint32_t numRetries() const override { return num_retries_; }
  uint32_t retryOn() const override { return retry_on_; }

  std::chrono::milliseconds per_try_timeout_{0};
  uint32_t num_retries_{};
  uint32_t retry_on_{};
};

class MockRetryState : public RetryState {
public:
  MockRetryState();
  ~MockRetryState();

  void expectRetry();

  MOCK_METHOD0(enabled, bool());
  MOCK_METHOD3(shouldRetry, RetryStatus(const Http::HeaderMap* response_headers,
                                        const Optional<Http::StreamResetReason>& reset_reason,
                                        DoRetryCallback callback));

  DoRetryCallback callback_;
};

class MockRateLimitPolicyEntry : public RateLimitPolicyEntry {
public:
  MockRateLimitPolicyEntry();
  ~MockRateLimitPolicyEntry();

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
  ~MockRateLimitPolicy();

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

  std::string cluster_;
  std::string runtime_key_;
};

class MockShadowWriter : public ShadowWriter {
public:
  MockShadowWriter();
  ~MockShadowWriter();

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
  const std::string& name() const override { return name_; }

  std::string name_{"fake_virtual_cluster"};
};

class MockVirtualHost : public VirtualHost {
public:
  MockVirtualHost();
  ~MockVirtualHost();

  // Router::VirtualHost
  MOCK_CONST_METHOD0(name, const std::string&());
  MOCK_CONST_METHOD0(rateLimitPolicy, const RateLimitPolicy&());
  MOCK_CONST_METHOD0(corsPolicy, const CorsPolicy*());
  MOCK_CONST_METHOD0(routeConfig, const Config&());

  std::string name_{"fake_vhost"};
  testing::NiceMock<MockRateLimitPolicy> rate_limit_policy_;
  TestCorsPolicy cors_policy_;
};

class MockHashPolicy : public HashPolicy {
public:
  MockHashPolicy();
  ~MockHashPolicy();

  // Router::HashPolicy
  MOCK_CONST_METHOD3(generateHash, Optional<uint64_t>(const std::string& downstream_address,
                                                      const Http::HeaderMap& headers,
                                                      const AddCookieCallback add_cookie));
};

class MockMetadataMatchCriteria : public MetadataMatchCriteria {
public:
  MockMetadataMatchCriteria();
  ~MockMetadataMatchCriteria();

  // Router::MetadataMatchCriteria
  MOCK_CONST_METHOD0(metadataMatchCriteria,
                     const std::vector<MetadataMatchCriterionConstSharedPtr>&());
};

class MockPathMatchCriterion : public PathMatchCriterion {
public:
  MockPathMatchCriterion();
  ~MockPathMatchCriterion();

  // Router::PathMatchCriterion
  MOCK_CONST_METHOD0(matchType, PathMatchType());
  MOCK_CONST_METHOD0(matcher, const std::string&());

  PathMatchType type_;
  std::string matcher_;
};

class MockRouteEntry : public RouteEntry {
public:
  MockRouteEntry();
  ~MockRouteEntry();

  // Router::Config
  MOCK_CONST_METHOD0(clusterName, const std::string&());
  MOCK_CONST_METHOD0(clusterNotFoundResponseCode, Http::Code());
  MOCK_CONST_METHOD2(finalizeRequestHeaders,
                     void(Http::HeaderMap& headers, const RequestInfo::RequestInfo& request_info));
  MOCK_CONST_METHOD2(finalizeResponseHeaders,
                     void(Http::HeaderMap& headers, const RequestInfo::RequestInfo& request_info));
  MOCK_CONST_METHOD0(hashPolicy, const HashPolicy*());
  MOCK_CONST_METHOD0(metadataMatchCriteria, const Router::MetadataMatchCriteria*());
  MOCK_CONST_METHOD0(priority, Upstream::ResourcePriority());
  MOCK_CONST_METHOD0(rateLimitPolicy, const RateLimitPolicy&());
  MOCK_CONST_METHOD0(retryPolicy, const RetryPolicy&());
  MOCK_CONST_METHOD0(shadowPolicy, const ShadowPolicy&());
  MOCK_CONST_METHOD0(timeout, std::chrono::milliseconds());
  MOCK_CONST_METHOD1(virtualCluster, const VirtualCluster*(const Http::HeaderMap& headers));
  MOCK_CONST_METHOD0(virtualHostName, const std::string&());
  MOCK_CONST_METHOD0(virtualHost, const VirtualHost&());
  MOCK_CONST_METHOD0(autoHostRewrite, bool());
  MOCK_CONST_METHOD0(useWebSocket, bool());
  MOCK_CONST_METHOD0(opaqueConfig, const std::multimap<std::string, std::string>&());
  MOCK_CONST_METHOD0(includeVirtualHostRateLimits, bool());
  MOCK_CONST_METHOD0(corsPolicy, const CorsPolicy*());
  MOCK_CONST_METHOD0(metadata, const envoy::api::v2::core::Metadata&());
  MOCK_CONST_METHOD0(pathMatchCriterion, const PathMatchCriterion&());

  std::string cluster_name_{"fake_cluster"};
  std::multimap<std::string, std::string> opaque_config_;
  TestVirtualCluster virtual_cluster_;
  TestRetryPolicy retry_policy_;
  testing::NiceMock<MockRateLimitPolicy> rate_limit_policy_;
  TestShadowPolicy shadow_policy_;
  testing::NiceMock<MockVirtualHost> virtual_host_;
  MockHashPolicy hash_policy_;
  MockMetadataMatchCriteria metadata_matches_criteria_;
  TestCorsPolicy cors_policy_;
  testing::NiceMock<MockPathMatchCriterion> path_match_criterion_;
};

class MockDecorator : public Decorator {
public:
  MockDecorator();
  ~MockDecorator();

  // Router::Decorator
  MOCK_CONST_METHOD0(getOperation, const std::string&());
  MOCK_CONST_METHOD1(apply, void(Tracing::Span& span));

  std::string operation_{"fake_operation"};
};

class MockRoute : public Route {
public:
  MockRoute();
  ~MockRoute();

  // Router::Route
  MOCK_CONST_METHOD0(directResponseEntry, const DirectResponseEntry*());
  MOCK_CONST_METHOD0(routeEntry, const RouteEntry*());
  MOCK_CONST_METHOD0(decorator, const Decorator*());

  testing::NiceMock<MockRouteEntry> route_entry_;
  testing::NiceMock<MockDecorator> decorator_;
};

class MockConfig : public Config {
public:
  MockConfig();
  ~MockConfig();

  // Router::Config
  MOCK_CONST_METHOD2(route, RouteConstSharedPtr(const Http::HeaderMap&, uint64_t random_value));
  MOCK_CONST_METHOD0(internalOnlyHeaders, const std::list<Http::LowerCaseString>&());
  MOCK_CONST_METHOD0(name, const std::string&());

  std::shared_ptr<MockRoute> route_;
  std::list<Http::LowerCaseString> internal_only_headers_;
  std::string name_{"fake_config"};
};

class MockRouteConfigProviderManager : public ServerRouteConfigProviderManager {
public:
  MockRouteConfigProviderManager();
  ~MockRouteConfigProviderManager();

  MOCK_METHOD0(routeConfigProviders, std::vector<RdsRouteConfigProviderSharedPtr>());
  MOCK_METHOD5(getRouteConfigProvider,
               RouteConfigProviderSharedPtr(
                   const envoy::config::filter::network::http_connection_manager::v2::Rds& rds,
                   Upstream::ClusterManager& cm, Stats::Scope& scope,
                   const std::string& stat_prefix, Init::Manager& init_manager));
  MOCK_METHOD1(removeRouteConfigProvider, void(const std::string& identifier));
};

} // namespace Router
} // namespace Envoy
