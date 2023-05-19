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

    // Update endpoints of default cluster `cluster_0` to `original_dst` cluster.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
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
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ClusterProvidedIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ClusterProvidedIntegrationTest, NormalLoadBalancing) {
  initialize();

  for (uint64_t i = 0; i < 4; i++) {
    for (size_t i = 0; i < fake_upstreams_.size(); i++) {
      codec_client_ = makeHttpConnection(lookupPort("http"));

      const auto& upstream_target_address = fake_upstreams_[i]->localAddress()->asString();

      Http::TestRequestHeaderMapImpl request_headers{
          {":method", "GET"},
          {":path", "/"},
          {":scheme", "http"},
          {":authority", "example.com"},
          {"x-envoy-original-dst-host", upstream_target_address}};

      auto response = codec_client_->makeRequestWithBody(request_headers, 0);

      auto upstream_index = waitForNextUpstreamRequest({0, 1, 2});
      EXPECT_EQ(upstream_index.value(), i);

      upstream_request_->encodeHeaders(default_response_headers_, true);

      ASSERT_TRUE(response->waitForEndStream());

      EXPECT_TRUE(upstream_request_->complete());
      EXPECT_TRUE(response->complete());

      cleanupUpstreamAndDownstream();
    }
  }
}

} // namespace
} // namespace ClusterProvided
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
