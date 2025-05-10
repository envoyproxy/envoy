#include "test/integration/filters/common.h"
#include "test/integration/http_integration.h"
#include "test/integration/http_protocol_integration.h"

#include "gtest/gtest.h"

using testing::AssertionResult;
using testing::Not;
using testing::TestWithParam;
using testing::ValuesIn;

namespace Envoy {

class DecodeDelayAndContinueFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "decode-delay-and-continue";

  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    decode_delay_timer_ = decoder_callbacks_->dispatcher().createTimer(
        [this]() -> void { decoder_callbacks_->continueDecoding(); });
    decode_delay_timer_->enableTimer(std::chrono::milliseconds(10));
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }

private:
  Event::TimerPtr decode_delay_timer_;
};

constexpr char DecodeDelayAndContinueFilter::name[];

static Registry::RegisterFactory<SimpleFilterConfig<DecodeDelayAndContinueFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    decoder_register_;

class EncodeDelayAndContinueFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "encode-delay-and-continue";

  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override {
    encode_delay_timer_ = encoder_callbacks_->dispatcher().createTimer(
        [this]() -> void { encoder_callbacks_->continueEncoding(); });
    encode_delay_timer_->enableTimer(std::chrono::milliseconds(10));
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }

private:
  Event::TimerPtr encode_delay_timer_;
};

constexpr char EncodeDelayAndContinueFilter::name[];

static Registry::RegisterFactory<SimpleFilterConfig<EncodeDelayAndContinueFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    encoder_register_;

class BandwidthLimitIntegrationTest : public HttpProtocolIntegrationTest {

protected:
  Buffer::OwnedImpl request_body_;
  Buffer::OwnedImpl response_body_;
};

INSTANTIATE_TEST_SUITE_P(
    Protocols, BandwidthLimitIntegrationTest,
    // TODO(nezdolik): fix test for HTTP3
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(BandwidthLimitIntegrationTest, ChunkedBodyNotCroppedOnContinueDecoding) {
  // Use h1, set up the test.
  setDownstreamProtocol(Http::CodecType::HTTP1);

  config_helper_.prependFilter(R"EOF(
    name: decode-delay-and-continue
  )EOF");
  config_helper_.prependFilter(ConfigHelper::defaultBandwidthLimitFilter());
  HttpProtocolIntegrationTest::initialize();

  // Start a client connection and start request.
  Http::TestRequestHeaderMapImpl headers{
      {":method", "POST"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "host"}};

  auto conn = makeClientConnection(lookupPort("http"));
  codec_client_ = makeHttpConnection(std::move(conn));

  auto encoder_decoder = codec_client_->startRequest(headers);
  auto response = std::move(encoder_decoder.second);

  // Split the request body into two parts.
  TestUtility::feedBufferWithRandomCharacters(request_body_, 10240);
  std::string initial_body = request_body_.toString().substr(0, 6981);
  std::string final_body = request_body_.toString().substr(6981, 10240);

  // Send the first part of the request body without ending the stream.
  codec_client_->sendData(encoder_decoder.first, initial_body, false);
  timeSystem().advanceTimeWait(std::chrono::milliseconds(1));
  // Send the rest of the data and end the stream.
  codec_client_->sendData(encoder_decoder.first, final_body, true);

  waitForNextUpstreamRequest();
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(upstream_request_->receivedData());
  EXPECT_EQ(request_body_.length(), upstream_request_->bodyLength());
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
}

TEST_P(BandwidthLimitIntegrationTest, ChunkedBodyCroppedOnContinueEncoding) {
  // Use h1, set up the test.
  setDownstreamProtocol(Http::CodecType::HTTP1);

  config_helper_.prependFilter(ConfigHelper::defaultBandwidthLimitFilter());
  config_helper_.prependFilter(R"EOF(
    name: encode-delay-and-continue
  )EOF");
  HttpProtocolIntegrationTest::initialize();

  // Start a client connection and start request.
  Http::TestRequestHeaderMapImpl headers{
      {":method", "POST"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "host"}};

  auto conn = makeClientConnection(lookupPort("http"));
  codec_client_ = makeHttpConnection(std::move(conn));

  auto encoder_decoder = codec_client_->startRequest(headers);
  auto response = std::move(encoder_decoder.second);

  TestUtility::feedBufferWithRandomCharacters(request_body_, 10000);
  codec_client_->sendData(encoder_decoder.first, request_body_.toString(), true);

  waitForNextUpstreamRequest();
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(upstream_request_->receivedData());
  EXPECT_EQ(request_body_.length(), upstream_request_->bodyLength());
  upstream_request_->encodeHeaders(default_response_headers_, false);
  // Split the response body into two parts.
  TestUtility::feedBufferWithRandomCharacters(response_body_, 10000);
  std::string initial_body = response_body_.toString().substr(0, 2000);
  std::string final_body = response_body_.toString().substr(2000, 8000);
  upstream_request_->encodeData(initial_body, false);
  timeSystem().advanceTimeWait(std::chrono::milliseconds(1));
  upstream_request_->encodeData(final_body, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response_body_.length(), response->body().length());
}

} // namespace Envoy
