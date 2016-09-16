#pragma once

#include "envoy/router/router.h"
#include "envoy/router/shadow_writer.h"

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
  uint32_t numRetries() const override { return num_retries_; }
  uint32_t retryOn() const override { return retry_on_; }

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

class TestRateLimitPolicy : public RateLimitPolicy {
public:
  // Router::RateLimitPolicy
  bool doGlobalLimiting() const override { return do_global_limiting_; }

  bool do_global_limiting_{};
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
  Upstream::ResourcePriority priority() const override { return priority_; }

  std::string name_{"fake_virtual_cluster"};
  Upstream::ResourcePriority priority_{Upstream::ResourcePriority::Default};
};

class MockRouteEntry : public RouteEntry {
public:
  MockRouteEntry();
  ~MockRouteEntry();

  // Router::Config
  MOCK_CONST_METHOD0(clusterName, const std::string&());
  MOCK_CONST_METHOD1(finalizeRequestHeaders, void(Http::HeaderMap& headers));
  MOCK_CONST_METHOD0(priority, Upstream::ResourcePriority());
  MOCK_CONST_METHOD0(rateLimitPolicy, const RateLimitPolicy&());
  MOCK_CONST_METHOD0(retryPolicy, const RetryPolicy&());
  MOCK_CONST_METHOD0(shadowPolicy, const ShadowPolicy&());
  MOCK_CONST_METHOD0(timeout, std::chrono::milliseconds());
  MOCK_CONST_METHOD1(virtualCluster, const VirtualCluster*(const Http::HeaderMap& headers));
  MOCK_CONST_METHOD0(virtualHostName, const std::string&());

  std::string cluster_name_{"fake_cluster"};
  std::string vhost_name_{"fake_vhost"};
  TestVirtualCluster virtual_cluster_;
  TestRetryPolicy retry_policy_;
  TestRateLimitPolicy rate_limit_policy_;
  TestShadowPolicy shadow_policy_;
};

class MockConfig : public Config {
public:
  MockConfig();
  ~MockConfig();

  // Router::Config
  MOCK_CONST_METHOD2(redirectRequest,
                     const RedirectEntry*(const Http::HeaderMap& headers, uint64_t random_value));
  MOCK_CONST_METHOD2(routeForRequest,
                     const RouteEntry*(const Http::HeaderMap&, uint64_t random_value));
  MOCK_CONST_METHOD0(internalOnlyHeaders, const std::list<Http::LowerCaseString>&());
  MOCK_CONST_METHOD0(responseHeadersToAdd,
                     const std::list<std::pair<Http::LowerCaseString, std::string>>&());
  MOCK_CONST_METHOD0(responseHeadersToRemove, const std::list<Http::LowerCaseString>&());

  std::list<Http::LowerCaseString> internal_only_headers_;
  std::list<std::pair<Http::LowerCaseString, std::string>> response_headers_to_add_;
  std::list<Http::LowerCaseString> response_headers_to_remove_;
};

class MockStableRouteTable : public StableRouteTable {
public:
  MockStableRouteTable();
  ~MockStableRouteTable();

  // Router::StableRouteTable
  MOCK_CONST_METHOD1(redirectRequest, const RedirectEntry*(const Http::HeaderMap& headers));
  MOCK_CONST_METHOD1(routeForRequest, const RouteEntry*(const Http::HeaderMap&));

  testing::NiceMock<MockRouteEntry> route_entry_;
};

} // Router
