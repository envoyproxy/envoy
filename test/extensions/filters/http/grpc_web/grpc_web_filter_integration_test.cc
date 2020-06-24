#include <memory>

#include "extensions/filters/http/well_known_names.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class GrpcWebFilterIntegrationTest : public ::testing::TestWithParam<Network::Address::IpVersion>,
                                     public HttpIntegrationTest {
public:
  GrpcWebFilterIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void SetUp() override {
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
    config_helper_.addFilter("name: envoy.filters.http.grpc_web");
  }
};
INSTANTIATE_TEST_SUITE_P(IpVersions, GrpcWebFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(GrpcWebFilterIntegrationTest, GRPCWebTrailersNotDuplicated) {
  config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
  setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);

  Http::TestRequestTrailerMapImpl request_trailers{{"request1", "trailer1"},
                                                   {"request2", "trailer2"}};
  Http::TestResponseTrailerMapImpl response_trailers{{"response1", "trailer1"},
                                                     {"response2", "trailer2"}};

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {"content-type", "application/grpc-web"},
                                     {":authority", "host"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, 1, false);
  codec_client_->sendTrailers(*request_encoder_, request_trailers);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(1, false);
  upstream_request_->encodeTrailers(response_trailers);
  response->waitForEndStream();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1, upstream_request_->bodyLength());
  EXPECT_THAT(*upstream_request_->trailers(), HeaderMapEqualRef(&request_trailers));

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_TRUE(absl::StrContains(response->body(), "response1:trailer1"));
  EXPECT_TRUE(absl::StrContains(response->body(), "response2:trailer2"));
  // Expect that the trailers be in the response-body instead
  EXPECT_EQ(response->trailers(), nullptr);
}

} // namespace
} // namespace Envoy
