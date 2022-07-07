#include "test/integration/http_protocol_integration.h"

namespace Envoy {
namespace {

class HttpAsyncBodyInjectionIntegrationTest : public HttpProtocolIntegrationTest {
protected:
  void testWithEndStreamAtBody() {
    initialize();
    codec_client_ = makeHttpConnection(lookupPort("http"));

    // Send request with no trailers.
    auto response = codec_client_->makeRequestWithBody(default_request_headers_, "request_body");
    waitForNextUpstreamRequest();

    // Make sure that the body was properly propagated.
    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_TRUE(upstream_request_->receivedData());
    EXPECT_EQ(upstream_request_->body().toString(), "request_body");

    // Send response with no trailers.
    upstream_request_->encodeHeaders(default_response_headers_, false);
    upstream_request_->encodeData("response_body", true);
    ASSERT_TRUE(response->waitForEndStream());

    // Make sure that the body was properly propagated.
    EXPECT_TRUE(response->complete());
    EXPECT_EQ(response->body(), "response_body");
  }

  void testWithEndStreamAtTrailers() {
    initialize();
    codec_client_ = makeHttpConnection(lookupPort("http"));

    // Send request with trailers.
    auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
    request_encoder_ = &encoder_decoder.first;
    auto response = std::move(encoder_decoder.second);
    codec_client_->sendData(*request_encoder_, "request_body", false);
    Http::TestRequestTrailerMapImpl request_trailers{{"trailer-key", "request"}};
    codec_client_->sendTrailers(*request_encoder_, request_trailers);
    waitForNextUpstreamRequest();

    // Make sure that the body and trailers were properly propagated.
    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_TRUE(upstream_request_->receivedData());
    EXPECT_EQ(upstream_request_->body().toString(), "request_body");
    ASSERT_TRUE(upstream_request_->trailers());
    EXPECT_EQ(upstream_request_->trailers()->size(), 1);

    // Send response with trailers.
    upstream_request_->encodeHeaders(default_response_headers_, false);
    upstream_request_->encodeData("response_body", false);
    Http::TestResponseTrailerMapImpl response_trailers{{"trailer-key", "response"}};
    upstream_request_->encodeTrailers(response_trailers);
    ASSERT_TRUE(response->waitForEndStream());

    // Make sure that the body and trailers were properly propagated.
    EXPECT_TRUE(response->complete());
    EXPECT_EQ(response->body(), "response_body");
    ASSERT_TRUE(response->trailers());
    EXPECT_EQ(response->trailers()->size(), 1);
  }
};

// Send request/response with NO trailers. Tests filter chain with single filter.
// Works as expected.
TEST_P(HttpAsyncBodyInjectionIntegrationTest, EndStreamAtBodySingleFilter) {
  config_helper_.prependFilter(R"EOF(
  name: async-inject-body-at-end-stream-filter
  )EOF");

  testWithEndStreamAtBody();
}

// Send request/response with NO trailers. Tests filter chain with multiple filters,
// including one that buffers the whole body. Works as expected.
TEST_P(HttpAsyncBodyInjectionIntegrationTest, EndStreamAtBodyRealisticFilters) {
  config_helper_.prependFilter(R"EOF(
  name: async-inject-body-at-end-stream-filter
  )EOF");
  config_helper_.prependFilter(Envoy::ConfigHelper::defaultBufferFilter());
  config_helper_.prependFilter(R"EOF(
  name: async-inject-body-at-end-stream-filter
  )EOF");

  testWithEndStreamAtBody();
}
// Send request/response WITH trailers. Tests filter chain with single filter.
// Works as expected.
TEST_P(HttpAsyncBodyInjectionIntegrationTest, EndStreamAtTrailersSingleFilter) {
  config_helper_.prependFilter(R"EOF(
  name: async-inject-body-at-end-stream-filter
  )EOF");

  testWithEndStreamAtTrailers();
}
// Send request/response WITH trailers. Tests filter chain with multiple filters,
// including one that buffers the whole body.
//
// Previously, this test would result in
// `upstream_reset_before_response_started{connection_termination}` because the
// request body would be sent twice to the same filter by the filter manager.
TEST_P(HttpAsyncBodyInjectionIntegrationTest, EndStreamAtTrailersRealisticFilters) {
  config_helper_.prependFilter(R"EOF(
  name: async-inject-body-at-end-stream-filter
  )EOF");
  config_helper_.prependFilter(Envoy::ConfigHelper::defaultBufferFilter());
  config_helper_.prependFilter(R"EOF(
  name: async-inject-body-at-end-stream-filter
  )EOF");

  testWithEndStreamAtTrailers();
}

INSTANTIATE_TEST_SUITE_P(Protocols, HttpAsyncBodyInjectionIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             /*downstream_protocols=*/{Http::CodecType::HTTP2},
                             /*upstream_protocols=*/{Http::CodecType::HTTP2})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

} // namespace
} // namespace Envoy
