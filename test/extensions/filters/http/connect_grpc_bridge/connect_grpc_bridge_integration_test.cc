#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class ConnectIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                               public HttpIntegrationTest {
public:
  ConnectIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void SetUp() override {
    const std::string filter =
        R"yaml(
            name: connect_grpc_bridge
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.connect_grpc_bridge.v3.FilterConfig
        )yaml";
    config_helper_.prependFilter(filter);
  }
};

TEST_P(ConnectIntegrationTest, ConnectFilterResponseBufferLimit) {
  config_helper_.setBufferLimits(1024, 1024);
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/Service/Method"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/proto"},
                                     {"connect-protocol-version", "1"}});
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"},
                                      {"content-type", "application/grpc+proto"}},
      false);
  upstream_request_->encodeData(1024 * 65, false);
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("500"));
}

INSTANTIATE_TEST_SUITE_P(IpVersions, ConnectIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

} // namespace
} // namespace Envoy
