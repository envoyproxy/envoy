#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class GrpcStatsDirectResponseIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  GrpcStatsDirectResponseIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void SetUp() override {
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void {
          auto* route_config = hcm.mutable_route_config();
          auto* route = route_config->mutable_virtual_hosts(0)->mutable_routes(0);
          route->mutable_match()->set_prefix("/");
          auto* direct_response = route->mutable_direct_response();
          direct_response->set_status(404);
          direct_response->mutable_body()->set_inline_string("not found");
        });

    const std::string filter =
        R"yaml(
            name: envoy.filters.http.grpc_stats
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.grpc_stats.v3.FilterConfig
              stats_for_all_methods: true
              enable_upstream_stats: true
        )yaml";
    config_helper_.prependFilter(filter);
    initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, GrpcStatsDirectResponseIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(GrpcStatsDirectResponseIntegrationTest, ConnectRequestToDirectResponse) {
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/anything"},
                                     {":scheme", "http"},
                                     {":authority", "127.0.0.1"},
                                     {"content-type", "application/connect+proto"}});

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("404", response->headers().getStatusValue());
  EXPECT_EQ("not found", response->body());
}

} // namespace
} // namespace Envoy
