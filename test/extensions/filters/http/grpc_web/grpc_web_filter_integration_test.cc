#include <memory>

#include "extensions/filters/http/well_known_names.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

using Envoy::Http::HeaderValueOf;

namespace Envoy {
namespace {

using TestParams = std::tuple<Network::Address::IpVersion, Http::CodecClient::Type>;

class GrpcWebFilterIntegrationTest : public testing::TestWithParam<TestParams>,
                                     public HttpIntegrationTest {
public:
  GrpcWebFilterIntegrationTest()
      : HttpIntegrationTest(std::get<1>(GetParam()), std::get<0>(GetParam())) {}

  void SetUp() override {
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
    config_helper_.addFilter("name: envoy.filters.http.grpc_web");
  }

  static std::string testParamsToString(const testing::TestParamInfo<TestParams> params) {
    return fmt::format(
        "{}_{}",
        TestUtility::ipTestParamsToString(testing::TestParamInfo<Network::Address::IpVersion>(
            std::get<0>(params.param), params.index)),
        (std::get<1>(params.param) == Http::CodecClient::Type::HTTP2 ? "Http2" : "Http"));
  }
};

INSTANTIATE_TEST_SUITE_P(
    Params, GrpcWebFilterIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::Values(Http::CodecClient::Type::HTTP1,
                                     Http::CodecClient::Type::HTTP2)),
    GrpcWebFilterIntegrationTest::testParamsToString);

TEST_P(GrpcWebFilterIntegrationTest, GrpcWebTrailersNotDuplicated) {
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

  // We expect to have response trailers when the downstream is HTTP/2
  // (https://github.com/envoyproxy/envoy/issues/10514).
  if (std::get<1>(GetParam()) == Http::CodecClient::Type::HTTP2) {
    EXPECT_THAT(*response->trailers(), HeaderValueOf("response1", "trailer1"));
    EXPECT_THAT(*response->trailers(), HeaderValueOf("response2", "trailer2"));
  } else {
    // Expect that the trailers be in the response-body instead.
    EXPECT_EQ(nullptr, response->trailers());
  }
}

} // namespace
} // namespace Envoy
