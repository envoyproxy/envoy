#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.validate.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/load_balancing_policies/deterministic_aperture/v3/deterministic_aperture.pb.h"

#include "test/integration/http_integration.h"

#include "absl/strings/str_replace.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace DeterministicAperture {

class DeterministicApertureCustomLbTest : public HttpIntegrationTest, public testing::Test {
public:
  DeterministicApertureCustomLbTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, TestEnvironment::getIpVersionsForTest().front(),
                            ConfigHelper::httpProxyConfig()) {
    autonomous_upstream_ = true;
    setUpstreamCount(num_hosts_);

    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* static_resources = bootstrap.mutable_static_resources();
      auto* cluster = static_resources->mutable_clusters(0);

      cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::LOAD_BALANCING_POLICY_CONFIG);
      auto mutable_lb_policy = cluster->mutable_load_balancing_policy();
      envoy::config::cluster::v3::LoadBalancingPolicy::Policy lbpolicy;
      envoy::extensions::load_balancing_policies::deterministic_aperture::v3::
          DeterministicApertureLbConfig deterministic_aperture_config;
      lbpolicy.mutable_typed_extension_config()->set_name(
          "envoy.load_balancing_policies.deterministic_aperture");
      lbpolicy.mutable_typed_extension_config()->mutable_typed_config()->PackFrom(
          deterministic_aperture_config);

      mutable_lb_policy->add_policies()->MergeFrom(lbpolicy);
      cluster->clear_load_assignment();

      auto* load_assignment = cluster->mutable_load_assignment();
      load_assignment->set_cluster_name(cluster->name());
      auto* endpoints = load_assignment->add_endpoints();
      for (uint32_t i = 0; i < num_hosts_; i++) {
        auto* lb_endpoint = endpoints->add_lb_endpoints();

        // ConfigHelper will fill in ports later.
        auto* endpoint = lb_endpoint->mutable_endpoint();
        auto* addr = endpoint->mutable_address()->mutable_socket_address();
        addr->set_address(Network::Test::getLoopbackAddressString(
            TestEnvironment::getIpVersionsForTest().front()));
        addr->set_port_value(0);
      }
    });

    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          auto* vhost = hcm.mutable_route_config()->mutable_virtual_hosts(0);

          auto* resp_header = vhost->add_response_headers_to_add();
          auto* header = resp_header->mutable_header();
          header->set_key(host_header_);
          header->set_value("%UPSTREAM_REMOTE_ADDRESS%");

          vhost->clear_routes();
          configureRoute(vhost->add_routes());
        });
  }

  void configureRoute(envoy::config::route::v3::Route* route) {
    auto* match = route->mutable_match();
    match->set_prefix("/");
    auto* action = route->mutable_route();
    action->set_cluster("cluster_0");
    action->add_hash_policy()->mutable_header()->set_header_name(hash_header_);
  };

  void SetUp() override {
    setDownstreamProtocol(Http::CodecType::HTTP1);
    setUpstreamProtocol(Http::CodecType::HTTP1);
  }

  void runTest(Http::TestRequestHeaderMapImpl& request_headers, const int n = 1,
               const int m = 1000) {
    ASSERT_LT(n, m);

    std::set<std::string> hosts;
    for (int i = 0; i < m; i++) {
      Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};

      // Send header only request.
      IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
      ASSERT_TRUE(response->waitForEndStream());
      // Record the upstream address.
      hosts.emplace(response->headers()
                        .get(Envoy::Http::LowerCaseString{host_header_})[0]
                        ->value()
                        .getStringView());

      if (i >= n) {
        break;
      }
    }

    EXPECT_GT(hosts.size(), 0);
  }

  const uint32_t num_hosts_{4};

  const std::string hash_header_{"x-hash"};
  const std::string host_header_{"x-host"};

  Http::TestRequestHeaderMapImpl request_headers_{{":method", "GET"},  {":path", "/test"},
                                                  {":scheme", "http"}, {":authority", "host"},
                                                  {"x-type", "a"},     {"x-hash", "hash-a"}};
};

TEST_F(DeterministicApertureCustomLbTest, BasicLoadBalancer) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  runTest(request_headers_);
}

} // namespace DeterministicAperture
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
