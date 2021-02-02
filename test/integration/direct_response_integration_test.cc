#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
class DirectResponseIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                      public HttpIntegrationTest {
public:
  DirectResponseIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void TearDown() override { cleanupUpstreamAndDownstream(); }

  // The default value for body size in bytes is 4096.
  void testDirectResponseBodySize(uint32_t body_size_bytes = 4096) {
    const std::string body_content(body_size_bytes, 'a');
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void {
          auto* route_config = hcm.mutable_route_config();
          route_config->mutable_max_direct_response_body_size_bytes()->set_value(body_size_bytes);

          auto* route = route_config->mutable_virtual_hosts(0)->mutable_routes(0);

          route->mutable_match()->set_prefix("/direct");

          auto* direct_response = route->mutable_direct_response();
          direct_response->set_status(200);
          direct_response->mutable_body()->set_inline_string(body_content);
        });

    initialize();
    codec_client_ = makeHttpConnection(lookupPort("http"));

    auto encoder_decoder = codec_client_->startRequest(Http::TestRequestHeaderMapImpl{
        {":method", "POST"},
        {":path", "/direct"},
        {":scheme", "http"},
        {":authority", "host"},
    });
    auto response = std::move(encoder_decoder.second);
    response->waitForEndStream();
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
    EXPECT_EQ(body_size_bytes, response->body().size());
    EXPECT_EQ(body_content, response->body());
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DirectResponseIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DirectResponseIntegrationTest, DefaultDirectResponseBodySize) {
  // The default of direct response body size is 4KB.
  testDirectResponseBodySize();
}

TEST_P(DirectResponseIntegrationTest, DirectResponseBodySizeLarge) {
  // Test with a large direct response body size, and with constrained buffer limits.
  config_helper_.setBufferLimits(1024, 1024);
  testDirectResponseBodySize(1000 * 4096);
}

TEST_P(DirectResponseIntegrationTest, DirectResponseBodySizeSmall) {
  testDirectResponseBodySize(1);
}

} // namespace Envoy
