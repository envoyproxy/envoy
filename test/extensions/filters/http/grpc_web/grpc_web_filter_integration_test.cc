#include <memory>

#include "extensions/filters/http/well_known_names.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

using SkipEncodingEmptyTrailers = bool;
using TestParams =
    std::tuple<Network::Address::IpVersion, Http::CodecClient::Type, SkipEncodingEmptyTrailers>;

class GrpcWebFilterIntegrationTest : public testing::TestWithParam<TestParams>,
                                     public HttpIntegrationTest {
public:
  GrpcWebFilterIntegrationTest()
      : HttpIntegrationTest(std::get<1>(GetParam()), std::get<0>(GetParam())) {}

  void SetUp() override {
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
    config_helper_.addFilter("name: envoy.filters.http.grpc_web");
  }

  void skipEncodingEmptyTrailers(SkipEncodingEmptyTrailers http2_skip_encoding_empty_trailers) {
    config_helper_.addRuntimeOverride(
        "envoy.reloadable_features.http2_skip_encoding_empty_trailers",
        http2_skip_encoding_empty_trailers ? "true" : "false");
  }

  static std::string testParamsToString(const testing::TestParamInfo<TestParams> params) {
    return fmt::format(
        "{}_{}_{}",
        TestUtility::ipTestParamsToString(testing::TestParamInfo<Network::Address::IpVersion>(
            std::get<0>(params.param), params.index)),
        std::get<1>(params.param) == Http::CodecClient::Type::HTTP2 ? "Http2" : "Http",
        std::get<2>(params.param) ? "SkipEncodingEmptyTrailers" : "SubmitEncodingEmptyTrailers");
  }
};

INSTANTIATE_TEST_SUITE_P(
    Params, GrpcWebFilterIntegrationTest,
    testing::Combine(
        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
        testing::Values(Http::CodecClient::Type::HTTP1, Http::CodecClient::Type::HTTP2),
        testing::Values(SkipEncodingEmptyTrailers{true}, SkipEncodingEmptyTrailers{false})),
    GrpcWebFilterIntegrationTest::testParamsToString);

TEST_P(GrpcWebFilterIntegrationTest, GrpcWebTrailersNotDuplicated) {
  const auto downstream_protocol = std::get<1>(GetParam());
  const bool http2_skip_encoding_empty_trailers = std::get<2>(GetParam());

  if (downstream_protocol == Http::CodecClient::Type::HTTP1) {
    config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
  } else {
    skipEncodingEmptyTrailers(http2_skip_encoding_empty_trailers);
  }

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

  if (downstream_protocol == Http::CodecClient::Type::HTTP1) {
    // When the downstream protocol is HTTP/1.1 we expect the trailers to be in the response-body.
    EXPECT_EQ(nullptr, response->trailers());
  }

  if (downstream_protocol == Http::CodecClient::Type::HTTP2) {
    if (http2_skip_encoding_empty_trailers) {
      // When the downstream protocol is HTTP/2 and the feature-flag to skip encoding empty trailers
      // is turned on, expect that the trailers are included in the response-body.
      EXPECT_EQ(nullptr, response->trailers());
    } else {
      // Otherwise, we send empty trailers.
      ASSERT_NE(nullptr, response->trailers());
      EXPECT_TRUE(response->trailers()->empty());
    }
  }
}

} // namespace
} // namespace Envoy
