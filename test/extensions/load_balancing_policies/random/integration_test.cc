#include <chrono>
#include <cstdint>

#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

#include "source/common/common/base64.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/load_balancing_policies/random/config.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace Random {
namespace {

class RandomIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                              public HttpIntegrationTest {
public:
  RandomIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    // Create 3 different upstream server for stateful session test.
    setUpstreamCount(3);
  }

  void initializeConfig(bool legacy_api = false) {
    // Update endpoints of default cluster `cluster_0` to 3 different fake upstreams.
    config_helper_.addConfigModifier(
        [legacy_api](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
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

          // If legacy API is used, set the LB policy by the old way.
          if (legacy_api) {
            cluster_0->set_lb_policy(envoy::config::cluster::v3::Cluster::RANDOM);
            return;
          }

          auto* policy = cluster_0->mutable_load_balancing_policy();

          const std::string policy_yaml = R"EOF(
          policies:
          - typed_extension_config:
              name: envoy.load_balancing_policies.random
              typed_config:
                  "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.random.v3.Random
          )EOF";

          TestUtility::loadFromYaml(policy_yaml, *policy);
        });

    HttpIntegrationTest::initialize();
  }

  void runNormalLoadBalancing() {
    for (uint64_t i = 0; i < 8; i++) {
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
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, RandomIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(RandomIntegrationTest, NormalLoadBalancing) {
  initializeConfig();
  runNormalLoadBalancing();
}

TEST_P(RandomIntegrationTest, NormalLoadBalancingWithLegacyAPI) {
  initializeConfig(true);
  runNormalLoadBalancing();
}

} // namespace
} // namespace Random
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
