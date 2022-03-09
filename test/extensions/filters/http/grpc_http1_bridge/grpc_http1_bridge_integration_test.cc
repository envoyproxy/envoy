#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// A test class for testing HTTP/1.1 upstream and downstreams

class GrpcIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                            public HttpIntegrationTest {
public:
  GrpcIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}
};

// Test hitting the bridge filter with too many response bytes to buffer. Given
// the headers are not proxied, the connection manager will send a local error reply.
TEST_P(GrpcIntegrationTest, HittingGrpcFilterLimitBufferingHeaders) {
  config_helper_.prependFilter(
      "{ name: grpc_http1_bridge, typed_config: { \"@type\": "
      "type.googleapis.com/envoy.extensions.filters.http.grpc_http1_bridge.v3.Config } }");
  config_helper_.setBufferLimits(1024, 1024);
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/grpc"},
                                     {"x-envoy-retry-grpc-on", "cancelled"}});
  waitForNextUpstreamRequest();

  // Send the overly large response. Because the grpc_http1_bridge filter buffers and buffer
  // limits are exceeded, this will be translated into an unknown gRPC error.
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(1024 * 65, false);
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));
  EXPECT_THAT(response->headers(),
              HeaderValueOf(Http::Headers::get().GrpcStatus, "2")); // Unknown gRPC error
}

INSTANTIATE_TEST_SUITE_P(IpVersions, GrpcIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

} // namespace
} // namespace Envoy
