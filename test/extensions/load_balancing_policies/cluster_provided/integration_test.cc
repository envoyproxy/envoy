#include <chrono>
#include <cstdint>

#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

#include "source/common/common/base64.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/load_balancing_policies/cluster_provided/config.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace ClusterProvided {
namespace {

class ClusterProvidedIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                       public HttpIntegrationTest {
public:
  ClusterProvidedIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    // Create 3 different upstream server.
    setUpstreamCount(3);
  }

  void initializeConfig(bool legacy_api = false, bool disable_lagacy_api_conversion = false) {
    if (disable_lagacy_api_conversion) {
      config_helper_.addRuntimeOverride("envoy.reloadable_features.convert_legacy_lb_config",
                                        "false");
    }

    config_helper_.addConfigModifier(
        [legacy_api](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(0);
          ASSERT(cluster_0->name() == "cluster_0");

          std::string cluster_yaml = R"EOF(
            name: cluster_0
            connect_timeout: 1.250s
            type: ORIGINAL_DST
            lb_policy: CLUSTER_PROVIDED
            original_dst_lb_config:
              use_http_header: true
          )EOF";

          TestUtility::loadFromYaml(cluster_yaml, *cluster_0);

          // If legacy API is used, set the LB policy by the old way.
          if (legacy_api) {
            cluster_0->set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);
            return;
          }

          auto* policy = cluster_0->mutable_load_balancing_policy();

          const std::string policy_yaml = R"EOF(
          policies:
          - typed_extension_config:
              name: envoy.load_balancing_policies.cluster_provided
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.cluster_provided.v3.ClusterProvided
          )EOF";

          TestUtility::loadFromYaml(policy_yaml, *policy);
        });

    HttpIntegrationTest::initialize();
  }

  void runNormalLoadBalancing() {
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

INSTANTIATE_TEST_SUITE_P(IpVersions, ClusterProvidedIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test the case where the cluster provided load balancer is configured by the load balancing
// policy API and it works as expected.
TEST_P(ClusterProvidedIntegrationTest, NormalLoadBalancing) {
  initializeConfig();
  runNormalLoadBalancing();
}

// Test the case where the cluster provided load balancer is configured by the legacy cluster LB
// policy API and it works as expected.
TEST_P(ClusterProvidedIntegrationTest, NormalLoadBalancingWithLegacyAPI) {
  initializeConfig(true);
  runNormalLoadBalancing();
}

// Test the case where the cluster provided load balancer is configured by the legacy cluster LB
// policy API (but disable the API conversion) and it works as expected.
TEST_P(ClusterProvidedIntegrationTest, NormalLoadBalancingWithLegacyAPIAndDisableAPIConversion) {
  initializeConfig(true, true);
  runNormalLoadBalancing();
}

} // namespace
} // namespace ClusterProvided
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
