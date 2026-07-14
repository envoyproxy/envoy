#include <chrono>
#include <cstdint>

#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

#include "source/common/common/base64.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace PeakEwma {
namespace {

class PeakEwmaIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                public HttpIntegrationTest {
public:
  PeakEwmaIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    // Create 3 different upstream servers for testing P2C behavior
    setUpstreamCount(3);
  }

  void initializeConfig() {
    // Add Peak EWMA HTTP filter to record RTT samples.
    config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.peak_ewma
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.peak_ewma.v3alpha.PeakEwmaConfig
)EOF");

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(0);
      ASSERT(cluster_0->name() == "cluster_0");
      auto* endpoint = cluster_0->mutable_load_assignment()->mutable_endpoints()->Mutable(0);

      constexpr absl::string_view endpoints_yaml = R"EOF(
      lb_endpoints:
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
      - endpoint:
          address:
            socket_address:
              address: {}
              port_value: 0
      )EOF";

      const std::string local_address = Network::Test::getLoopbackAddressString(GetParam());
      TestUtility::loadFromYaml(
          fmt::format(endpoints_yaml, local_address, local_address, local_address), *endpoint);

      // Configure Peak EWMA load balancing policy.
      auto* policy = cluster_0->mutable_load_balancing_policy();

      const std::string policy_yaml = R"EOF(
      policies:
      - typed_extension_config:
          name: envoy.load_balancing_policies.peak_ewma
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.peak_ewma.v3alpha.PeakEwma
            decay_time: 0.100s
      )EOF";

      TestUtility::loadFromYaml(policy_yaml, *policy);
    });

    HttpIntegrationTest::initialize();
  }

  void runBasicLoadBalancing() {
    // Send multiple requests and verify they are distributed across upstreams
    std::set<int> used_upstreams;

    for (uint64_t i = 0; i < 10; i++) {
      codec_client_ = makeHttpConnection(lookupPort("http"));

      Http::TestRequestHeaderMapImpl request_headers{
          {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "example.com"}};

      auto response = codec_client_->makeRequestWithBody(request_headers, 0);

      auto upstream_index = waitForNextUpstreamRequest({0, 1, 2});
      ASSERT(upstream_index.has_value());
      used_upstreams.insert(upstream_index.value());

      upstream_request_->encodeHeaders(default_response_headers_, true);

      ASSERT_TRUE(response->waitForEndStream());

      EXPECT_TRUE(upstream_request_->complete());
      EXPECT_TRUE(response->complete());

      cleanupUpstreamAndDownstream();
    }

    EXPECT_GE(used_upstreams.size(), 2) << "P2C should distribute across multiple upstreams";
  }

  void runLatencySensitiveRouting() {
    // Warm up EWMA measurements with initial requests to establish latency baseline.
    for (int i = 0; i < 10; i++) {
      codec_client_ = makeHttpConnection(lookupPort("http"));

      Http::TestRequestHeaderMapImpl request_headers{
          {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "example.com"}};

      auto response = codec_client_->makeRequestWithBody(request_headers, 0);

      auto upstream_index = waitForNextUpstreamRequest({0, 1, 2});
      ASSERT(upstream_index.has_value());

      // Simulate different response times by delaying some upstreams.
      if (upstream_index.value() == 1) {
        // Add artificial delay for upstream 1 to make it "slower".
        timeSystem().advanceTimeWait(std::chrono::milliseconds(100));
      }

      upstream_request_->encodeHeaders(default_response_headers_, true);

      ASSERT_TRUE(response->waitForEndStream());
      EXPECT_TRUE(upstream_request_->complete());
      EXPECT_TRUE(response->complete());

      cleanupUpstreamAndDownstream();
    }

    // Now send more requests and verify Peak EWMA strongly avoids the slow upstream.
    // Peak EWMA should route < 10% traffic to slow server (vs 33% with round robin).
    std::map<int, int> upstream_counts;

    for (int i = 0; i < 100; i++) {
      codec_client_ = makeHttpConnection(lookupPort("http"));

      Http::TestRequestHeaderMapImpl request_headers{
          {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "example.com"}};

      auto response = codec_client_->makeRequestWithBody(request_headers, 0);

      auto upstream_index = waitForNextUpstreamRequest({0, 1, 2});
      ASSERT(upstream_index.has_value());
      upstream_counts[upstream_index.value()]++;

      // Continue to simulate delay for upstream 1.
      if (upstream_index.value() == 1) {
        timeSystem().advanceTimeWait(std::chrono::milliseconds(100));
      }

      upstream_request_->encodeHeaders(default_response_headers_, true);

      ASSERT_TRUE(response->waitForEndStream());
      EXPECT_TRUE(upstream_request_->complete());
      EXPECT_TRUE(response->complete());

      cleanupUpstreamAndDownstream();
    }

    int slow_upstream_requests = upstream_counts[1];
    int total_requests = 100;
    double slow_upstream_ratio = static_cast<double>(slow_upstream_requests) / total_requests;

    // Peak EWMA should strongly avoid slow servers (< 10% traffic vs 33% with round robin).
    // The HTTP filter records RTT samples, enabling the load balancer to make cost-based
    // decisions. This threshold validates the core performance characteristic and provides
    // regression protection.
    EXPECT_LT(slow_upstream_ratio, 0.10)
        << "Peak EWMA should strongly avoid slow upstream. Got " << slow_upstream_requests << "/"
        << total_requests << " (" << (slow_upstream_ratio * 100) << "%) requests to slow server";
  }

  void runConfigValidation() {
    codec_client_ = makeHttpConnection(lookupPort("http"));

    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "example.com"}};

    auto response = codec_client_->makeRequestWithBody(request_headers, 0);

    auto upstream_index = waitForNextUpstreamRequest({0, 1, 2});
    ASSERT(upstream_index.has_value());

    upstream_request_->encodeHeaders(default_response_headers_, true);

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_TRUE(response->complete());

    cleanupUpstreamAndDownstream();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, PeakEwmaIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(PeakEwmaIntegrationTest, BasicLoadBalancing) {
  initializeConfig();
  runBasicLoadBalancing();
}

TEST_P(PeakEwmaIntegrationTest, LatencySensitiveRouting) {
  initializeConfig();
  runLatencySensitiveRouting();
}

TEST_P(PeakEwmaIntegrationTest, ConfigurationValidation) {
  initializeConfig();
  runConfigValidation();
}

} // namespace
} // namespace PeakEwma
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
