#include <chrono>
#include <cstdint>

#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

#include "source/common/common/base64.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/load_balancing_policies/original_dst/config.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace OriginalDst {
namespace {

class OriginalDstIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                   public HttpIntegrationTest {
public:
  OriginalDstIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    setUpstreamCount(3);
  }

  void initializeConfig(bool use_load_balancing_policy = true) {
    config_helper_.addConfigModifier(
        [use_load_balancing_policy](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(0);
          ASSERT(cluster_0->name() == "cluster_0");

          if (use_load_balancing_policy) {
            // New API: use load_balancing_policy with typed original_dst config.
            // Do NOT set original_dst_lb_config to verify the typed config path works.
            std::string cluster_yaml = R"EOF(
              name: cluster_0
              connect_timeout: 1.250s
              type: ORIGINAL_DST
              lb_policy: CLUSTER_PROVIDED
              load_balancing_policy:
                policies:
                - typed_extension_config:
                    name: envoy.load_balancing_policies.original_dst
                    typed_config:
                      "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.original_dst.v3.OriginalDst
                      use_http_header: true
            )EOF";
            TestUtility::loadFromYaml(cluster_yaml, *cluster_0);
          } else {
            // Legacy API: use lb_policy: CLUSTER_PROVIDED with original_dst_lb_config.
            std::string cluster_yaml = R"EOF(
              name: cluster_0
              connect_timeout: 1.250s
              type: ORIGINAL_DST
              lb_policy: CLUSTER_PROVIDED
              original_dst_lb_config:
                use_http_header: true
            )EOF";
            TestUtility::loadFromYaml(cluster_yaml, *cluster_0);
          }
        });

    HttpIntegrationTest::initialize();
  }

  void runOriginalDstLoadBalancing() {
    for (uint64_t i = 0; i < 4; i++) {
      for (size_t upstream_index = 0; upstream_index < fake_upstreams_.size(); upstream_index++) {
        codec_client_ = makeHttpConnection(lookupPort("http"));

        const auto& upstream_target_address =
            fake_upstreams_[upstream_index]->localAddress()->asString();

        Http::TestRequestHeaderMapImpl request_headers{
            {":method", "GET"},
            {":path", "/"},
            {":scheme", "http"},
            {":authority", "example.com"},
            {"x-envoy-original-dst-host", upstream_target_address}};

        auto response = codec_client_->makeRequestWithBody(request_headers, 0);
        auto upstream = waitForNextUpstreamRequest({0, 1, 2});
        EXPECT_EQ(upstream.value(), upstream_index);

        upstream_request_->encodeHeaders(default_response_headers_, true);
        ASSERT_TRUE(response->waitForEndStream());
        EXPECT_TRUE(upstream_request_->complete());
        EXPECT_TRUE(response->complete());
        cleanupUpstreamAndDownstream();
      }
    }
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, OriginalDstIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test original_dst load balancing using the new load_balancing_policy extension point.
TEST_P(OriginalDstIntegrationTest, LoadBalancingWithNewApi) {
  initializeConfig(/*use_load_balancing_policy=*/true);
  runOriginalDstLoadBalancing();
}

// Test original_dst load balancing using the legacy lb_policy + original_dst_lb_config.
TEST_P(OriginalDstIntegrationTest, LoadBalancingWithLegacyApi) {
  initializeConfig(/*use_load_balancing_policy=*/false);
  runOriginalDstLoadBalancing();
}

} // namespace
} // namespace OriginalDst
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
