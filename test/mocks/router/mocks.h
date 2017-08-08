#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "envoy/router/router.h"
#include "envoy/router/router_ratelimit.h"
#include "envoy/router/shadow_writer.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Router {

class MockRedirectEntry : public RedirectEntry {
public:
  MockRedirectEntry();
  ~MockRedirectEntry();

  // Router::Config
  MOCK_CONST_METHOD1(newPath, std::string(const Http::HeaderMap& headers));
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
  MOCK_METHOD3(shouldRetry, bool(const Http::HeaderMap* response_headers,
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
                          const std::string& remote_address));

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

  std::string name_{"fake_vhost"};
  testing::NiceMock<MockRateLimitPolicy> rate_limit_policy_;
};

class MockHashPolicy : public HashPolicy {
public:
  MockHashPolicy();
  ~MockHashPolicy();

  // Router::HashPolicy
  MOCK_CONST_METHOD1(generateHash, Optional<uint64_t>(const Http::HeaderMap& headers));
};

class MockRouteEntry : public RouteEntry {
public:
  MockRouteEntry();
  ~MockRouteEntry();

  // Router::Config
  MOCK_CONST_METHOD0(clusterName, const std::string&());
  MOCK_CONST_METHOD1(finalizeRequestHeaders, void(Http::HeaderMap& headers));
  MOCK_CONST_METHOD0(hashPolicy, const HashPolicy*());
  MOCK_CONST_METHOD0(priority, Upstream::ResourcePriority());
  MOCK_CONST_METHOD0(rateLimitPolicy, const RateLimitPolicy&());
  MOCK_CONST_METHOD0(retryPolicy, const RetryPolicy&());
  MOCK_CONST_METHOD0(shadowPolicy, const ShadowPolicy&());
  MOCK_CONST_METHOD0(timeout, std::chrono::milliseconds());
  MOCK_CONST_METHOD1(virtualCluster, const VirtualCluster*(const Http::HeaderMap& headers));
  MOCK_CONST_METHOD0(virtualHostName, const std::string&());
  MOCK_CONST_METHOD0(virtualHost, const VirtualHost&());
  MOCK_CONST_METHOD0(autoHostRewrite, bool());
  MOCK_CONST_METHOD0(opaqueConfig, const std::multimap<std::string, std::string>&());
  MOCK_CONST_METHOD0(includeVirtualHostRateLimits, bool());

  std::string cluster_name_{"fake_cluster"};
  std::multimap<std::string, std::string> opaque_config_;
  TestVirtualCluster virtual_cluster_;
  TestRetryPolicy retry_policy_;
  testing::NiceMock<MockRateLimitPolicy> rate_limit_policy_;
  TestShadowPolicy shadow_policy_;
  testing::NiceMock<MockVirtualHost> virtual_host_;
  MockHashPolicy hash_policy_;
};

class MockRoute : public Route {
public:
  MockRoute();
  ~MockRoute();

  // Router::Route
  MOCK_CONST_METHOD0(redirectEntry, const RedirectEntry*());
  MOCK_CONST_METHOD0(routeEntry, const RouteEntry*());

  testing::NiceMock<MockRouteEntry> route_entry_;
};

class MockConfig : public Config {
public:
  MockConfig();
  ~MockConfig();

  // Router::Config
  MOCK_CONST_METHOD2(route, RouteConstSharedPtr(const Http::HeaderMap&, uint64_t random_value));
  MOCK_CONST_METHOD0(internalOnlyHeaders, const std::list<Http::LowerCaseString>&());
  MOCK_CONST_METHOD0(responseHeadersToAdd,
                     const std::list<std::pair<Http::LowerCaseString, std::string>>&());
  MOCK_CONST_METHOD0(responseHeadersToRemove, const std::list<Http::LowerCaseString>&());
  MOCK_CONST_METHOD0(usesRuntime, bool());

  std::shared_ptr<MockRoute> route_;
  std::list<Http::LowerCaseString> internal_only_headers_;
  std::list<std::pair<Http::LowerCaseString, std::string>> response_headers_to_add_;
  std::list<Http::LowerCaseString> response_headers_to_remove_;
};

} // namespace Router
} // namespace Envoy
