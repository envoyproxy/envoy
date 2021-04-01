#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class SetDelegatingRouteTest : public testing::TestWithParam<Network::Address::IpVersion>,
                               public HttpIntegrationTest {
public:
  SetDelegatingRouteTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, GetParam()) {}

  void initialize() override {
    config_helper_.addFilter(R"EOF(
    name: set-route-filter
    )EOF");

    setUpstreamCount(2);

    // Tests with ORIGINAL_DST cluster because the first use case of the setRoute / DelegatingRoute
    // route mutability functionality will be for a filter that re-routes requests to an
    // ORIGINAL_DST cluster on a per-request basis.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      std::string cluster_yaml = R"EOF(
              name: cluster_override
              connect_timeout: 1.250s
              type: ORIGINAL_DST
              lb_policy: CLUSTER_PROVIDED
              original_dst_lb_config:
                use_http_header: true
            )EOF";
      envoy::config::cluster::v3::Cluster cluster_config;
      TestUtility::loadFromYaml(cluster_yaml, cluster_config);
      auto* orig_dst_cluster = bootstrap.mutable_static_resources()->add_clusters();
      orig_dst_cluster->MergeFrom(cluster_config);
    });

    auto co_vhost = config_helper_.createVirtualHost("cluster_override vhost", "/some/path",
                                                     "cluster_override");
    config_helper_.addVirtualHost(co_vhost);

    HttpIntegrationTest::initialize();
  }
};

// Tests both IPv4 and IPv6
INSTANTIATE_TEST_SUITE_P(IpVersions, SetDelegatingRouteTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Tests that a filter (set-route-filter) using the setRoute callback and DelegatingRoute mechanism
// successfully overrides the cached route, and subsequently, the request's upstream cluster
// selection.
TEST_P(SetDelegatingRouteTest, SetRouteToDelegatingRouteWithClusterOverride) {
  initialize();

  std::string ip;
  if (GetParam() == Network::Address::IpVersion::v4) {
    ip = "127.0.0.1";
  } else {
    ip = "[::1]";
  }

  const std::string ip_port_pair =
      absl::StrCat(ip, ":", fake_upstreams_[1]->localAddress()->ip()->port());

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},
      {":path", "/some/path"},
      {":scheme", "http"},
      {":authority", "cluster_0"},
      {"x-envoy-original-dst-host", ip_port_pair},
  };

  codec_client_ = makeHttpConnection(lookupPort("http"));
  // Setting the upstream_index argument to 1 here tests that we get a request on
  // fake_upstreams_[1], which implies traffic is going to cluster_override. This is because
  // cluster_override, being an ORIGINAL DST cluster, will route the request to the IP/port
  // specified in the x-envoy-original-dst-host header (in this test case, port taken from
  // fake_upstreams_[1]).
  auto response =
      sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0, 1);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Even though headers specify cluster_0, set_route_filter modifies cached route cluster of
  // current request to cluster_override
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_cx_total")->value());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_rq_total")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_override.upstream_cx_total")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_override.upstream_rq_200")->value());
}

} // namespace
} // namespace Envoy
