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
namespace Subset {
namespace {

class SubsetIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                              public HttpIntegrationTest {
public:
  SubsetIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    setUpstreamCount(3);

    // Add the header to metadata filter to help set the metadata for subset load balancer.
    config_helper_.prependFilter(R"EOF(
    name: envoy.filters.http.header_to_metadata
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.header_to_metadata.v3.Config
      request_rules:
      - header: "version"
        on_header_present:
          metadata_namespace: "envoy.lb"
          key: "version"
    )EOF");
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
            metadata:
              filter_metadata:
                envoy.lb:
                  version: v1
                  stage: canary
          - endpoint:
              address:
                socket_address:
                  address: {}
                  port_value: 0
            metadata:
              filter_metadata:
                envoy.lb:
                  version: v2
                  stage: canary
          - endpoint:
              address:
                socket_address:
                  address: {}
                  port_value: 0
            metadata:
              filter_metadata:
                envoy.lb:
                  version: v3
          )EOF";

          const std::string local_address = Network::Test::getLoopbackAddressString(GetParam());
          TestUtility::loadFromYaml(
              fmt::format(endpoints_yaml, local_address, local_address, local_address), *endpoint);

          // If legacy API is used, set the LB policy by the old way.
          if (legacy_api) {
            // Set the inner LB policy of the subset LB policy to RANDOM.
            cluster_0->set_lb_policy(envoy::config::cluster::v3::Cluster::RANDOM);

            auto* mutable_subset_lb_config = cluster_0->mutable_lb_subset_config();

            const std::string subset_lb_config_yaml = R"EOF(
                fallback_policy: ANY_ENDPOINT
                subset_selectors:
                - keys:
                  - "version"
                  - "stage"
                  fallback_policy: NO_FALLBACK
                - keys:
                  - "version"
                  fallback_policy: ANY_ENDPOINT
                list_as_any: true
            )EOF";

            TestUtility::loadFromYaml(subset_lb_config_yaml, *mutable_subset_lb_config);
            return;
          }

          auto* policy = cluster_0->mutable_load_balancing_policy();

          const std::string policy_yaml = R"EOF(
          policies:
          - typed_extension_config:
              name: envoy.load_balancing_policies.subset
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.subset.v3.Subset
                fallback_policy: ANY_ENDPOINT
                subset_selectors:
                - keys:
                  - "version"
                  - "stage"
                  fallback_policy: NO_FALLBACK
                - keys:
                  - "version"
                  fallback_policy: ANY_ENDPOINT
                list_as_any: true
                subset_lb_policy:
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
    for (uint64_t i = 1; i <= 3; i++) {

      codec_client_ = makeHttpConnection(lookupPort("http"));

      Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                     {":path", "/"},
                                                     {":scheme", "http"},
                                                     {":authority", "example.com"},
                                                     {"version", fmt::format("v{}", i)}};

      auto response = codec_client_->makeRequestWithBody(request_headers, 0);

      waitForNextUpstreamRequest(i - 1);

      upstream_request_->encodeHeaders(default_response_headers_, true);

      ASSERT_TRUE(response->waitForEndStream());

      EXPECT_TRUE(upstream_request_->complete());
      EXPECT_TRUE(response->complete());

      cleanupUpstreamAndDownstream();
    }
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, SubsetIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(SubsetIntegrationTest, NormalLoadBalancing) {
  initializeConfig();
  runNormalLoadBalancing();
}

TEST_P(SubsetIntegrationTest, NormalLoadBalancingWithLegacyAPI) {
  initializeConfig(true);
  runNormalLoadBalancing();
}

} // namespace
} // namespace Subset
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
