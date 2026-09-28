#include <set>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace QuotaAware {
namespace {

constexpr absl::string_view kQuotaAwarePolicy = R"EOF(
policies:
- typed_extension_config:
    name: envoy.load_balancing_policies.quota_aware
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.quota_aware.v3.QuotaAware
      fallback_policy:
        policies:
        - typed_extension_config:
            name: envoy.load_balancing_policies.round_robin
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.round_robin.v3.RoundRobin
)EOF";

constexpr absl::string_view kRoundRobinPolicy = R"EOF(
policies:
- typed_extension_config:
    name: envoy.load_balancing_policies.round_robin
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.round_robin.v3.RoundRobin
)EOF";

class QuotaAwareIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                  public HttpIntegrationTest {
public:
  QuotaAwareIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    setUpstreamCount(2);
  }

  void initializeCluster(absl::string_view policy_yaml, bool two_priority = true) {
    const auto ip_version = GetParam();
    config_helper_.addConfigModifier(
        [policy_yaml, two_priority, ip_version](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(0);
          ASSERT_EQ(cluster_0->name(), "cluster_0");
          const std::string local_address = Network::Test::getLoopbackAddressString(ip_version);
          auto* assignment = cluster_0->mutable_load_assignment();
          assignment->clear_endpoints();
          if (two_priority) {
            TestUtility::loadFromYaml(fmt::format(R"EOF(
cluster_name: cluster_0
endpoints:
- priority: 0
  lb_endpoints:
  - endpoint:
      address:
        socket_address:
          address: {}
          port_value: 0
    metadata:
      filter_metadata:
        aigateway.envoy.io:
          per_route_rule_backend_name: "default/pt-east/route/r/rule/0/ref/0"
- priority: 1
  lb_endpoints:
  - endpoint:
      address:
        socket_address:
          address: {}
          port_value: 0
    metadata:
      filter_metadata:
        aigateway.envoy.io:
          per_route_rule_backend_name: "default/pt-west/route/r/rule/0/ref/1"
)EOF",
                                                  local_address, local_address),
                                      *assignment);
          } else {
            TestUtility::loadFromYaml(fmt::format(R"EOF(
cluster_name: cluster_0
endpoints:
- lb_endpoints:
  - endpoint:
      address:
        socket_address:
          address: {}
          port_value: 0
  - endpoint:
      address:
        socket_address:
          address: {}
          port_value: 0
)EOF",
                                                  local_address, local_address),
                                      *assignment);
          }
          TestUtility::loadFromYaml(std::string(policy_yaml),
                                    *cluster_0->mutable_load_balancing_policy());
        });
    HttpIntegrationTest::initialize();
  }

  Http::TestRequestHeaderMapImpl request_headers_{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "example.com"}};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, QuotaAwareIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(QuotaAwareIntegrationTest, MissingMetadataFailOpenUsesP0) {
  initializeCluster(kQuotaAwarePolicy);
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(request_headers_, 0);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(QuotaAwareIntegrationTest, P0ExhaustedPicksP1) {
  config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.set_metadata
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config
  metadata:
  - metadata_namespace: envoy.filters.http.ratelimit
    value:
      passedBackends:
      - backend_name: default/pt-west
        model_name_override: ""
)EOF");
  initializeCluster(kQuotaAwarePolicy);
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(request_headers_, 0);
  waitForNextUpstreamRequest(1);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(QuotaAwareIntegrationTest, SameBackendDifferentModelsPicksLiveRef) {
  config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.set_metadata
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config
  metadata:
  - metadata_namespace: envoy.filters.http.ratelimit
    value:
      passedBackends:
      - backend_name: nai-admin/openai
        model_name_override: gpt-4
)EOF");
  const auto ip_version = GetParam();
  config_helper_.addConfigModifier(
      [ip_version](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
        auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(0);
        ASSERT_EQ(cluster_0->name(), "cluster_0");
        const std::string local_address = Network::Test::getLoopbackAddressString(ip_version);
        auto* assignment = cluster_0->mutable_load_assignment();
        assignment->clear_endpoints();
        TestUtility::loadFromYaml(fmt::format(R"EOF(
cluster_name: cluster_0
endpoints:
- lb_endpoints:
  - endpoint:
      address:
        socket_address:
          address: {}
          port_value: 0
    metadata:
      filter_metadata:
        aigateway.envoy.io:
          per_route_rule_backend_name: "nai-admin/openai/route/r/rule/0/ref/0"
          model_name_override: "gpt-5"
  - endpoint:
      address:
        socket_address:
          address: {}
          port_value: 0
    metadata:
      filter_metadata:
        aigateway.envoy.io:
          per_route_rule_backend_name: "nai-admin/openai/route/r/rule/0/ref/1"
          model_name_override: "gpt-4"
)EOF",
                                              local_address, local_address),
                                  *assignment);
        TestUtility::loadFromYaml(std::string(kQuotaAwarePolicy),
                                  *cluster_0->mutable_load_balancing_policy());
      });
  HttpIntegrationTest::initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(request_headers_, 0);
  waitForNextUpstreamRequest(1);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(QuotaAwareIntegrationTest, EmptyPassedBackendsReturns429) {
  config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.set_metadata
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config
  metadata:
  - metadata_namespace: envoy.filters.http.ratelimit
    value:
      passedBackends: []
)EOF");
  initializeCluster(kQuotaAwarePolicy);
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(request_headers_, 0);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("429", response->headers().getStatusValue());
}

TEST_P(QuotaAwareIntegrationTest, ClusterWithoutPolicyStillRoundRobin) {
  initializeCluster(kRoundRobinPolicy, false);
  std::set<uint64_t> seen;
  for (int i = 0; i < 4; ++i) {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = codec_client_->makeRequestWithBody(request_headers_, 0);
    auto upstream_index = waitForNextUpstreamRequest({0, 1});
    ASSERT_TRUE(upstream_index.has_value());
    seen.insert(*upstream_index);
    upstream_request_->encodeHeaders(default_response_headers_, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());
    cleanupUpstreamAndDownstream();
  }
  EXPECT_EQ(2, seen.size());
}

} // namespace
} // namespace QuotaAware
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
