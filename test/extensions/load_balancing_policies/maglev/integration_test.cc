#include <chrono>
#include <cstdint>

#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

#include "source/common/common/base64.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/load_balancing_policies/maglev/config.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace Maglev {
namespace {

class MaglevIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                              public HttpIntegrationTest {
public:
  MaglevIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    // Create 3 different upstream server for stateful session test.
    setUpstreamCount(3);

    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          hcm.mutable_route_config()
              ->mutable_virtual_hosts(0)
              ->mutable_routes(0)
              ->mutable_route()
              ->add_hash_policy()
              ->mutable_header()
              ->set_header_name("x-hash");
        });
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
            cluster_0->set_lb_policy(envoy::config::cluster::v3::Cluster::MAGLEV);
            cluster_0->mutable_maglev_lb_config();
            return;
          }

          auto* policy = cluster_0->mutable_load_balancing_policy();

          const std::string policy_yaml = R"EOF(
          policies:
          - typed_extension_config:
              name: envoy.load_balancing_policies.maglev
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.maglev.v3.Maglev
          )EOF";

          TestUtility::loadFromYaml(policy_yaml, *policy);
        });

    HttpIntegrationTest::initialize();
  }

  void runNormalLoadBalancing() {
    absl::optional<uint64_t> unique_upstream_index;

    for (uint64_t i = 0; i < 8; i++) {
      codec_client_ = makeHttpConnection(lookupPort("http"));

      Http::TestRequestHeaderMapImpl request_headers{
          {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "example.com"},
          {"x-hash", "hash"},
      };

      auto response = codec_client_->makeRequestWithBody(request_headers, 0);

      auto upstream_index = waitForNextUpstreamRequest({0, 1, 2});
      ASSERT(upstream_index.has_value());

      if (unique_upstream_index.has_value()) {
        EXPECT_EQ(unique_upstream_index.value(), upstream_index.value());
      } else {
        unique_upstream_index = upstream_index.value();
      }

      upstream_request_->encodeHeaders(default_response_headers_, true);

      ASSERT_TRUE(response->waitForEndStream());

      EXPECT_TRUE(upstream_request_->complete());
      EXPECT_TRUE(response->complete());

      cleanupUpstreamAndDownstream();
    }
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, MaglevIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(MaglevIntegrationTest, NormalLoadBalancing) {
  initializeConfig();
  runNormalLoadBalancing();
}

TEST_P(MaglevIntegrationTest, NormalLoadBalancingWithLegacyAPI) {
  initializeConfig(true);
  runNormalLoadBalancing();
}

TEST_P(MaglevIntegrationTest, NormalLoadBalancingWithLegacyAPIAndDisableAPIConversion) {
  initializeConfig(true, true);
  runNormalLoadBalancing();
}

} // namespace
} // namespace Maglev
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
