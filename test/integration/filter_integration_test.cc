#include "test/integration/http_protocol_integration.h"

namespace Envoy {
namespace {

using testing::HasSubstr;

// For end to end tests checking behaviors of the filter chain.
class FilterIntegrationTest : public UpstreamDownstreamIntegrationTest {
public:
  void prependFilter(const std::string& config) {
    config_helper_.prependFilter(config, testing_downstream_filter_);
  }

  void initialize() override { UpstreamDownstreamIntegrationTest::initialize(); }

  template <class T> void changeHeadersForStopAllTests(T& headers, bool set_buffer_limit) {
    headers.addCopy("content_size", std::to_string(count_ * size_));
    headers.addCopy("added_size", std::to_string(added_decoded_data_size_));
    headers.addCopy("is_first_trigger", "value");
    if (set_buffer_limit) {
      headers.addCopy("buffer_limit", std::to_string(buffer_limit_));
    }
  }

  void verifyUpStreamRequestAfterStopAllFilter() {
    if (downstreamProtocol() != Http::CodecType::HTTP1) {
      // decode-headers-return-stop-all-filter calls addDecodedData in decodeData and
      // decodeTrailers. 2 decoded data were added.
      EXPECT_EQ(count_ * size_ + added_decoded_data_size_ * 2, upstream_request_->bodyLength());
    } else {
      EXPECT_EQ(count_ * size_ + added_decoded_data_size_ * 1, upstream_request_->bodyLength());
    }
    EXPECT_EQ(true, upstream_request_->complete());
  }

  void testNonTerminalEncodingFilterWithCompleteRequest();
  void testNonTerminalEncodingFilterWithIncompleteRequest();
  void testFilterAddsDataAndTrailersToHeaderOnlyRequest();

  const int count_ = 70;
  const int size_ = 1000;
  const int added_decoded_data_size_ = 1;
  const int buffer_limit_ = 100;
};

using MultiProtocolFilterIntegrationTest = FilterIntegrationTest;

INSTANTIATE_TEST_SUITE_P(Protocols, FilterIntegrationTest,
                         testing::ValuesIn(UpstreamDownstreamIntegrationTest::getDefaultTestParams(
                             {Http::CodecType::HTTP2}, {Http::CodecType::HTTP2})),
                         UpstreamDownstreamIntegrationTest::testParamsToString);

INSTANTIATE_TEST_SUITE_P(
    Protocols, MultiProtocolFilterIntegrationTest,
    testing::ValuesIn(UpstreamDownstreamIntegrationTest::getDefaultTestParams(
        {Http::CodecType::HTTP1, Http::CodecType::HTTP2, Http::CodecType::HTTP3},
        {Http::CodecType::HTTP1, Http::CodecType::HTTP2, Http::CodecType::HTTP3})),
    UpstreamDownstreamIntegrationTest::testParamsToString);

static std::string on_local_reply_filter = R"EOF(
name: on-local-reply-filter
)EOF";

TEST_P(FilterIntegrationTest, OnLocalReply) {
  prependFilter(on_local_reply_filter);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  // The filter will send a local reply when receiving headers, the client
  // should get a complete response.
  {
    auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("original_reply", response->body());
  }

  // The second two tests validate the filter intercepting local replies, but
  // upstream HTTP filters don't run on local replies.
  if (!testing_downstream_filter_) {
    return;
  }

  // The filter will send a local reply when receiving headers, and interrupt
  // that with a second reply sent from the encoder chain. The client will see
  // the second response.
  {
    default_request_headers_.addCopy("dual-local-reply", "yes");
    auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("second_reply", response->body());
  }
  // The filter will send a local reply when receiving headers and reset the
  // stream onLocalReply. The client will get a reset and no response even if
  // dual local replies are on (from the prior request).
  {
    default_request_headers_.addCopy("reset", "yes");
    auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    ASSERT_TRUE(response->waitForReset());
    ASSERT_FALSE(response->complete());
  }
}

TEST_P(FilterIntegrationTest, AddInvalidDecodedData) {
  EXPECT_ENVOY_BUG(
      {
        useAccessLog("%RESPONSE_CODE_DETAILS%");
        prependFilter(R"EOF(
  name: add-invalid-data-filter
  )EOF");
        initialize();
        codec_client_ = makeHttpConnection(lookupPort("http"));
        auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
        waitForNextUpstreamRequest();
        upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
        ASSERT_TRUE(response->waitForEndStream());
        EXPECT_EQ("502", response->headers().getStatusValue());
        EXPECT_THAT(waitForAccessLog(access_log_name_),
                    HasSubstr("filter_added_invalid_request_data"));
      },
      "Invalid request data");
}

// Verifies behavior for https://github.com/envoyproxy/envoy/pull/11248
TEST_P(FilterIntegrationTest, AddBodyToRequestAndWaitForIt) {
  prependFilter(R"EOF(
  name: wait-for-whole-request-and-response-filter
  )EOF");
  prependFilter(R"EOF(
  name: add-body-filter
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  EXPECT_EQ("body", upstream_request_->body().toString());
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  // encode data, as we have a separate test for the transforming header only response.
  upstream_request_->encodeData(128, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(FilterIntegrationTest, AddBodyToResponseAndWaitForIt) {
  prependFilter(R"EOF(
  name: add-body-filter
  )EOF");
  prependFilter(R"EOF(
  name: wait-for-whole-request-and-response-filter
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 128);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("body", response->body());
}

// Verify that filters can add a body and trailers to a header-only request or
// response.
TEST_P(FilterIntegrationTest, AddBodyAndTrailer) {
  prependFilter(R"EOF(
  name: add-body-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.AddBodyFilterConfig
      add_trailers_in_encode_decode_header: true
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  EXPECT_EQ("body", upstream_request_->body().toString());
  EXPECT_EQ("dummy_request_trailer_value",
            upstream_request_->trailers()
                ->get(Http::LowerCaseString("dummy_request_trailer"))[0]
                ->value()
                .getStringView());

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("body", response->body());
  EXPECT_EQ("dummy_response_trailer_value",
            response->trailers()
                ->get(Http::LowerCaseString("dummy_response_trailer"))[0]
                ->value()
                .getStringView());
}

TEST_P(FilterIntegrationTest, MissingHeadersLocalReplyDownstreamBytesCount) {
  useAccessLog("%DOWNSTREAM_WIRE_BYTES_SENT% %DOWNSTREAM_WIRE_BYTES_RECEIVED% "
               "%DOWNSTREAM_HEADER_BYTES_SENT% %DOWNSTREAM_HEADER_BYTES_RECEIVED%");
  prependFilter("{ name: invalid-header-filter }");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Missing method
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"remove-method", "yes"},
                                     {"send-reply", "yes"}});
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  if (testing_downstream_filter_) {
    expectDownstreamBytesSentAndReceived(BytesCountExpectation(90, 88, 71, 54),
                                         BytesCountExpectation(40, 58, 40, 58),
                                         BytesCountExpectation(7, 10, 7, 8));
  }
}

TEST_P(FilterIntegrationTest, MissingHeadersLocalReplyUpstreamBytesCount) {
  useAccessLog("%UPSTREAM_WIRE_BYTES_SENT% %UPSTREAM_WIRE_BYTES_RECEIVED% "
               "%UPSTREAM_HEADER_BYTES_SENT% %UPSTREAM_HEADER_BYTES_RECEIVED%");
  prependFilter("{ name: invalid-header-filter }");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Missing method
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"remove-method", "yes"},
                                     {"send-reply", "yes"}});
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  expectUpstreamBytesSentAndReceived(BytesCountExpectation(0, 0, 0, 0),
                                     BytesCountExpectation(0, 0, 0, 0),
                                     BytesCountExpectation(0, 0, 0, 0));
}

TEST_P(FilterIntegrationTest, MissingHeadersLocalReplyWithBody) {
  useAccessLog("%RESPONSE_CODE_DETAILS%");
  prependFilter("{ name: invalid-header-filter }");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Missing method
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"remove-method", "yes"},
                                     {"send-reply", "yes"}},
      1024);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("invalid_header_filter_ready"));
}

TEST_P(FilterIntegrationTest, MissingHeadersLocalReplyWithBodyBytesCount) {
  useAccessLog("%DOWNSTREAM_WIRE_BYTES_SENT% %DOWNSTREAM_WIRE_BYTES_RECEIVED% "
               "%DOWNSTREAM_HEADER_BYTES_SENT% %DOWNSTREAM_HEADER_BYTES_RECEIVED%");
  prependFilter("{ name: invalid-header-filter }");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Missing method
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"remove-method", "yes"},
                                     {"send-reply", "yes"}},
      1024);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  if (testing_downstream_filter_) {
    // When testing an upstream HTTP filters, we may receive body bytes before we
    // process headers, so don't set expectations.
    expectDownstreamBytesSentAndReceived(BytesCountExpectation(109, 1152, 90, 81),
                                         BytesCountExpectation(0, 58, 0, 58),
                                         BytesCountExpectation(7, 10, 7, 8));
  }
}

// Test buffering and then continuing after too many response bytes to buffer.
TEST_P(FilterIntegrationTest, BufferContinue) {
  // Bytes sent is configured for http/2 flow control windows.
  if (upstreamProtocol() != Http::CodecType::HTTP2) {
    return;
  }
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* route_config = hcm.mutable_route_config();
        auto* virtual_host = route_config->mutable_virtual_hosts(0);
        auto* header = virtual_host->mutable_response_headers_to_add()->Add()->mutable_header();
        header->set_key("foo");
        header->set_value("bar");
      });

  useAccessLog();
  prependFilter("{ name: buffer-continue-filter }");
  config_helper_.setBufferLimits(1024, 1024);
  initialize();

  // Send the request.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  auto downstream_request = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  Buffer::OwnedImpl data("HTTP body content goes here");
  codec_client_->sendData(*downstream_request, data, true);
  waitForNextUpstreamRequest();

  // Send the response headers.
  upstream_request_->encodeHeaders(default_response_headers_, false);

  // Now send an overly large response body. At some point, too much data will
  // be buffered, the stream will be reset, and the connection will disconnect.
  upstream_request_->encodeData(512, false);
  upstream_request_->encodeData(1024 * 100, false);

  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
    ASSERT_TRUE(fake_upstream_connection_->close());
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  }

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("500", response->headers().getStatusValue());
}

TEST_P(FilterIntegrationTest, ContinueHeadersOnlyInjectBodyFilter) {
  prependFilter(R"EOF(
  name: continue-headers-only-inject-body-filter
  )EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send a headers only request.
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Make sure that the body was injected to the request.
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(upstream_request_->receivedData());
  EXPECT_EQ(upstream_request_->body().toString(), "body");

  // Send a headers only response.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());

  // Make sure that the body was injected to the response.
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->body(), "body");
}

TEST_P(FilterIntegrationTest, StopIterationHeadersInjectBodyFilter) {
  prependFilter(R"EOF(
  name: stop-iteration-headers-inject-body-filter
  )EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send a headers only request.
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Make sure that the body was injected to the request.
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(upstream_request_->receivedData());
  EXPECT_EQ(upstream_request_->body().toString(), "body");

  // Send a headers only response.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());

  // Make sure that the body was injected to the response.
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->body(), "body");
}

TEST_P(FilterIntegrationTest, AddEncodedTrailers) {
  prependFilter(R"EOF(
name: add-trailers-filter
)EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 128);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, false);
  upstream_request_->encodeData(128, true);
  ASSERT_TRUE(response->waitForEndStream());

  if (upstreamProtocol() != Http::CodecType::HTTP1) {
    EXPECT_EQ("decode", upstream_request_->trailers()
                            ->get(Http::LowerCaseString("grpc-message"))[0]
                            ->value()
                            .getStringView());
  }
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
  if (downstream_protocol_ != Http::CodecType::HTTP1) {
    EXPECT_EQ("encode", response->trailers()->getGrpcMessageValue());
  }
}

// Tests missing headers needed for H/1 codec first line.
TEST_P(FilterIntegrationTest, DownstreamRequestWithFaultyFilter) {
  useAccessLog("%RESPONSE_CODE_DETAILS%");
  prependFilter("{ name: invalid-header-filter }");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Missing method
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"remove-method", "yes"}});
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_THAT(waitForAccessLog(access_log_name_), testing::MatchesRegex(".*required.*header.*"));

  // Missing path for non-CONNECT
  response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"remove-path", "yes"}});
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_THAT(waitForAccessLog(access_log_name_, 1), testing::MatchesRegex(".*required.*header.*"));
}

TEST_P(FilterIntegrationTest, FaultyFilterWithConnect) {
  // Faulty filter that removed host in a CONNECT request.
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        ConfigHelper::setConnectConfig(hcm, false, false,
                                       downstreamProtocol() == Http::CodecType::HTTP3);
      });
  useAccessLog("%RESPONSE_CODE_DETAILS%");
  prependFilter("{ name: invalid-header-filter }");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Missing host for CONNECT
  auto headers = Http::TestRequestHeaderMapImpl{
      {":method", "CONNECT"}, {":scheme", "http"}, {":authority", "www.host.com:80"}};

  auto response = std::move((codec_client_->startRequest(headers)).second);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_THAT(waitForAccessLog(access_log_name_), testing::MatchesRegex(".*required.*header.*"));
}

// Test hitting the decoder buffer filter with too many request bytes to buffer. Ensure the
// connection manager sends a 413.
TEST_P(FilterIntegrationTest, HittingDecoderFilterLimit) {
  prependFilter("{ name: encoder-decoder-buffer-filter }");
  config_helper_.setBufferLimits(1024, 1024);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/dynamo/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "5xx"}},
      1024 * 65);

  ASSERT_TRUE(response->waitForEndStream());
  // With HTTP/1 there's a possible race where if the connection backs up early,
  // the 413-and-connection-close may be sent while the body is still being
  // sent, resulting in a write error and the connection being closed before the
  // response is read.
  if (downstream_protocol_ != Http::CodecType::HTTP1) {
    ASSERT_TRUE(response->complete());
  }
  if (response->complete()) {
    EXPECT_EQ("413", response->headers().getStatusValue());
  }
}

// Test hitting the encoder buffer filter with too many response bytes to buffer. Given the request
// headers are sent on early, the stream/connection will be reset.
TEST_P(FilterIntegrationTest, HittingEncoderFilterLimit) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* route_config = hcm.mutable_route_config();
        auto* virtual_host = route_config->mutable_virtual_hosts(0);
        auto* header = virtual_host->mutable_response_headers_to_add()->Add()->mutable_header();
        header->set_key("foo");
        header->set_value("bar");
      });

  useAccessLog();
  prependFilter("{ name: encoder-decoder-buffer-filter }");
  config_helper_.setBufferLimits(1024, 1024);
  initialize();

  // Send the request.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  auto downstream_request = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  Buffer::OwnedImpl data("HTTP body content goes here");
  codec_client_->sendData(*downstream_request, data, true);
  waitForNextUpstreamRequest();

  // Send the response headers.
  upstream_request_->encodeHeaders(default_response_headers_, false);

  // Now send an overly large response body. At some point, too much data will
  // be buffered, the stream will be reset, and the connection will disconnect.
  upstream_request_->encodeData(1024 * 65, false);
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
    ASSERT_TRUE(fake_upstream_connection_->close());
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  }

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("500", response->headers().getStatusValue());
  // Regression test all sendLocalReply paths add route-requested headers.
  auto foo = Http::LowerCaseString("foo");
  ASSERT_FALSE(response->headers().get(foo).empty());
  EXPECT_EQ("bar", response->headers().get(foo)[0]->value().getStringView());

  // Regression test https://github.com/envoyproxy/envoy/issues/9881 by making
  // sure this path does standard HCM header transformations.
  EXPECT_TRUE(response->headers().Date() != nullptr);
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("500"));
  test_server_->waitForCounterEq("http.config_test.downstream_rq_5xx", 1);
}

TEST_P(FilterIntegrationTest, LocalReplyDuringEncoding) {
  prependFilter(R"EOF(
name: local-reply-during-encode
)EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"}});

  // Wait for the upstream request and begin sending a response with end_stream = false.
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("500", response->headers().getStatusValue());
  EXPECT_EQ(0, upstream_request_->body().length());
}

TEST_P(FilterIntegrationTest, LocalReplyDuringEncodingData) {
  prependFilter(R"EOF(
name: local-reply-during-encode-data
)EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // Wait for the upstream request and begin sending a response with end_stream = false.
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(size_, false);
  Http::TestResponseTrailerMapImpl response_trailers{{"response", "trailer"}};
  upstream_request_->encodeTrailers(response_trailers);

  // Response was aborted after headers were sent to the client.
  // The stream was reset. Client does not receive body or trailers.
  ASSERT_TRUE(response->waitForReset());
  EXPECT_FALSE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(0, response->body().length());
  EXPECT_EQ(nullptr, response->trailers());
}

// Tests StopAllIterationAndBuffer. Verifies decode-headers-return-stop-all-filter calls decodeData
// once after iteration is resumed.
TEST_P(FilterIntegrationTest, TestDecodeHeadersReturnsStopAll) {
  prependFilter(R"EOF(
name: call-decodedata-once-filter
)EOF");
  prependFilter(R"EOF(
name: decode-headers-return-stop-all-filter
)EOF");
  prependFilter(R"EOF(
name: passthrough-filter
)EOF");

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Sends a request with headers and data.
  changeHeadersForStopAllTests(default_request_headers_, false);
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  for (int i = 0; i < count_ - 1; i++) {
    codec_client_->sendData(*request_encoder_, size_, false);
  }
  // Sleeps for 1s in order to be consistent with testDecodeHeadersReturnsStopAllWatermark.
  absl::SleepFor(absl::Seconds(1));
  codec_client_->sendData(*request_encoder_, size_, true);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ(count_ * size_ + added_decoded_data_size_, upstream_request_->bodyLength());
  EXPECT_EQ(true, upstream_request_->complete());

  // Sends a request with headers, data, and trailers.
  auto encoder_decoder_2 = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder_2.first;
  response = std::move(encoder_decoder_2.second);
  for (int i = 0; i < count_; i++) {
    codec_client_->sendData(*request_encoder_, size_, false);
  }
  Http::TestRequestTrailerMapImpl request_trailers{{"trailer", "trailer"}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  verifyUpStreamRequestAfterStopAllFilter();
}

// Tests StopAllIterationAndWatermark. decode-headers-return-stop-all-filter sets buffer
// limit to 100. Verifies data pause when limit is reached, and resume after iteration continues.
TEST_P(FilterIntegrationTest, TestDecodeHeadersReturnsStopAllWatermark) {
  prependFilter(R"EOF(
name: decode-headers-return-stop-all-filter
)EOF");
  prependFilter(R"EOF(
name: passthrough-filter
)EOF");

  // Sets initial stream window to min value to make the client sensitive to a low watermark.
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_http2_protocol_options()->mutable_initial_stream_window_size()->set_value(
            ::Envoy::Http2::Utility::OptionsLimits::MIN_INITIAL_STREAM_WINDOW_SIZE);
      });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Sends a request with headers and data.
  changeHeadersForStopAllTests(default_request_headers_, true);
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  for (int i = 0; i < count_ - 1; i++) {
    codec_client_->sendData(*request_encoder_, size_, false);
  }
  // Gives buffer 1s to react to buffer limit.
  absl::SleepFor(absl::Seconds(1));
  codec_client_->sendData(*request_encoder_, size_, true);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ(count_ * size_ + added_decoded_data_size_, upstream_request_->bodyLength());
  EXPECT_EQ(true, upstream_request_->complete());

  // Sends a request with headers, data, and trailers.
  auto encoder_decoder_2 = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder_2.first;
  response = std::move(encoder_decoder_2.second);
  for (int i = 0; i < count_ - 1; i++) {
    codec_client_->sendData(*request_encoder_, size_, false);
  }
  // Gives buffer 1s to react to buffer limit.
  absl::SleepFor(absl::Seconds(1));
  codec_client_->sendData(*request_encoder_, size_, false);
  Http::TestRequestTrailerMapImpl request_trailers{{"trailer", "trailer"}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  verifyUpStreamRequestAfterStopAllFilter();
}

// Test two filters that return StopAllIterationAndBuffer back-to-back.
TEST_P(FilterIntegrationTest, TestTwoFiltersDecodeHeadersReturnsStopAll) {
  prependFilter(R"EOF(
name: decode-headers-return-stop-all-filter
)EOF");
  prependFilter(R"EOF(
name: decode-headers-return-stop-all-filter
)EOF");
  prependFilter(R"EOF(
name: passthrough-filter
)EOF");

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Sends a request with headers and data.
  changeHeadersForStopAllTests(default_request_headers_, false);
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  for (int i = 0; i < count_ - 1; i++) {
    codec_client_->sendData(*request_encoder_, size_, false);
  }
  codec_client_->sendData(*request_encoder_, size_, true);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ(count_ * size_ + added_decoded_data_size_, upstream_request_->bodyLength());
  EXPECT_EQ(true, upstream_request_->complete());

  // Sends a request with headers, data, and trailers.
  auto encoder_decoder_2 = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder_2.first;
  response = std::move(encoder_decoder_2.second);
  for (int i = 0; i < count_; i++) {
    codec_client_->sendData(*request_encoder_, size_, false);
  }
  Http::TestRequestTrailerMapImpl request_trailers{{"trailer", "trailer"}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  verifyUpStreamRequestAfterStopAllFilter();
}

// Tests encodeHeaders() returns StopAllIterationAndBuffer.
TEST_P(FilterIntegrationTest, TestEncodeHeadersReturnsStopAll) {
  prependFilter(R"EOF(
name: encode-headers-return-stop-all-filter
)EOF");
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.mutable_http2_protocol_options()->set_allow_metadata(true); });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Upstream responds with headers, data and trailers.
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();

  changeHeadersForStopAllTests(default_response_headers_, false);
  upstream_request_->encodeHeaders(default_response_headers_, false);
  for (int i = 0; i < count_ - 1; i++) {
    upstream_request_->encodeData(size_, false);
  }
  // Sleeps for 1s in order to be consistent with testEncodeHeadersReturnsStopAllWatermark.
  absl::SleepFor(absl::Seconds(1));
  upstream_request_->encodeData(size_, false);
  Http::TestResponseTrailerMapImpl response_trailers{{"response", "trailer"}};
  upstream_request_->encodeTrailers(response_trailers);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  // Data is added in encodeData for all protocols, and encodeTrailers for HTTP/2 and above.
  int times_added = upstreamProtocol() == Http::CodecType::HTTP1 ? 1 : 2;
  EXPECT_EQ(count_ * size_ + added_decoded_data_size_ * times_added, response->body().size());
}

// Tests encodeHeaders() returns StopAllIterationAndWatermark.
TEST_P(MultiProtocolFilterIntegrationTest, TestEncodeHeadersReturnsStopAllWatermark) {
  prependFilter(R"EOF(
name: encode-headers-return-stop-all-filter
)EOF");
  config_helper_.setBufferLimits(1024, 1024);
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.mutable_http2_protocol_options()->set_allow_metadata(true); });

  // Sets initial stream window to min value to make the upstream sensitive to a low watermark.
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_http2_protocol_options()->mutable_initial_stream_window_size()->set_value(
            ::Envoy::Http2::Utility::OptionsLimits::MIN_INITIAL_STREAM_WINDOW_SIZE);
      });
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_http2_protocol_options()->mutable_initial_stream_window_size()->set_value(
            ::Envoy::Http2::Utility::OptionsLimits::MIN_INITIAL_STREAM_WINDOW_SIZE);
      });
  if (upstreamProtocol() == Http::CodecType::HTTP2) {
    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() >= 1, "");
          ConfigHelper::HttpProtocolOptions protocol_options;
          protocol_options.mutable_explicit_http_config()
              ->mutable_http2_protocol_options()
              ->mutable_initial_stream_window_size()
              ->set_value(::Envoy::Http2::Utility::OptionsLimits::MIN_INITIAL_STREAM_WINDOW_SIZE);
          ConfigHelper::setProtocolOptions(
              *bootstrap.mutable_static_resources()->mutable_clusters(0), protocol_options);
        });
  }

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Upstream responds with headers, data and trailers.
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();

  changeHeadersForStopAllTests(default_response_headers_, true);
  upstream_request_->encodeHeaders(default_response_headers_, false);
  for (int i = 0; i < count_ - 1; i++) {
    upstream_request_->encodeData(size_, false);
  }
  // Gives buffer 1s to react to buffer limit.
  absl::SleepFor(absl::Seconds(1));
  upstream_request_->encodeData(size_, false);
  Http::TestResponseTrailerMapImpl response_trailers{{"response", "trailer"}};
  upstream_request_->encodeTrailers(response_trailers);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  // Data is added in encodeData for all protocols, and encodeTrailers for HTTP/2 and above.
  int times_added = upstreamProtocol() == Http::CodecType::HTTP1 ? 1 : 2;
  EXPECT_EQ(count_ * size_ + added_decoded_data_size_ * times_added, response->body().size());
}

// Tests a filter that returns a FilterHeadersStatus::Continue after a local reply without
// processing new metadata generated in decodeHeader
TEST_P(FilterIntegrationTest, LocalReplyWithMetadata) {
  prependFilter(R"EOF(
  name: local-reply-with-metadata-filter
  )EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  // Send a headers only request.
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  ASSERT_EQ("200", response->headers().getStatusValue());
}

static std::string remove_response_headers_filter = R"EOF(
name: remove-response-headers-filter
)EOF";

TEST_P(FilterIntegrationTest, HeadersOnlyRequestWithRemoveResponseHeadersFilter) {
  prependFilter(remove_response_headers_filter);
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  ASSERT_TRUE(response->waitForEndStream());
  // If a filter chain removes :status from the response headers, then Envoy must reply with
  // BadGateway and must not crash.
  ASSERT_TRUE(codec_client_->connected());
  EXPECT_EQ("502", response->headers().getStatusValue());
  EXPECT_THAT(response->body(), HasSubstr("missing required header: :status"));
}

TEST_P(FilterIntegrationTest, RemoveResponseHeadersFilter) {
  prependFilter(remove_response_headers_filter);
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  IntegrationStreamDecoderPtr response =
      codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  ASSERT_TRUE(response->waitForEndStream());
  // If a filter chain removes :status from the response headers, then Envoy must reply with
  // BadGateway and not crash.
  ASSERT_TRUE(codec_client_->connected());
  EXPECT_EQ("502", response->headers().getStatusValue());
  EXPECT_THAT(response->body(), HasSubstr("missing required header: :status"));
}
// Verify that when a filter encodeHeaders callback overflows response buffer in filter manager the
// filter chain is aborted and 500 is sent to the client.
TEST_P(FilterIntegrationTest, OverflowEncoderBufferFromEncodeHeaders) {
  config_helper_.setBufferLimits(64 * 1024, 64 * 1024);
  prependFilter(R"EOF(
  name: add-body-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.AddBodyFilterConfig
      where_to_add_body: ENCODE_HEADERS
      body_size: 70000
  )EOF");
  prependFilter(R"EOF(
  name: crash-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.CrashFilterConfig
      crash_in_encode_headers: false
      crash_in_encode_data: false
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  auto response = sendRequestAndWaitForResponse(default_request_headers_, 0, response_headers, 10,
                                                0, TestUtility::DefaultTimeout);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("500", response->headers().getStatusValue());
}

// Verify that when a filter encodeData callback overflows response buffer in filter manager the
// filter chain is aborted and 500 is sent to the client in case where upstream response headers
// have not yet been sent.
TEST_P(FilterIntegrationTest, OverflowEncoderBufferFromEncodeDataWithResponseHeadersUnsent) {
  config_helper_.setBufferLimits(64 * 1024, 64 * 1024);
  // Buffer filter will stop iteration from encodeHeaders preventing response headers from being
  // sent downstream.
  prependFilter(R"EOF(
  name: encoder-decoder-buffer-filter
  )EOF");
  prependFilter(R"EOF(
  name: crash-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.CrashFilterConfig
      crash_in_encode_headers: true
      crash_in_encode_data: true
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, false);
  // This much data should overflow the 64Kb response buffer.
  upstream_request_->encodeData(16 * 1024, false);
  upstream_request_->encodeData(64 * 1024, false);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("500", response->headers().getStatusValue());
}

// Verify that when a filter encodeData callback overflows response buffer in filter manager the
// filter chain is aborted and stream is reset in case where upstream response headers have already
// been sent.
TEST_P(FilterIntegrationTest, OverflowEncoderBufferFromEncodeData) {
  config_helper_.setBufferLimits(64 * 1024, 64 * 1024);
  // Make the add-body-filter stop iteration from encodeData. Headers should be sent to the client.
  prependFilter(R"EOF(
  name: add-body-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.AddBodyFilterConfig
      where_to_add_body: ENCODE_DATA
      where_to_stop_and_buffer: ENCODE_DATA
      body_size: 16384
  )EOF");
  prependFilter(R"EOF(
  name: crash-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.CrashFilterConfig
      crash_in_encode_headers: false
      crash_in_encode_data: true
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, false);
  // This much data should cause the add-body-filter to overflow response buffer
  upstream_request_->encodeData(16 * 1024, false);
  upstream_request_->encodeData(64 * 1024, false);
  ASSERT_TRUE(response->waitForReset());
  EXPECT_FALSE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Verify that when a filter decodeHeaders callback overflows request buffer in filter manager the
// filter chain is aborted and 413 is sent to the client.
TEST_P(FilterIntegrationTest, OverflowDecoderBufferFromDecodeHeaders) {
  config_helper_.setBufferLimits(64 * 1024, 64 * 1024);
  prependFilter(R"EOF(
  name: crash-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.CrashFilterConfig
      crash_in_decode_headers: true
  )EOF");
  prependFilter(R"EOF(
  name: add-body-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.AddBodyFilterConfig
      where_to_add_body: DECODE_HEADERS
      body_size: 70000
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("413", response->headers().getStatusValue());
}

// Verify that when a filter decodeData callback overflows request buffer in filter manager the
// filter chain is aborted and 413 is sent to the client.
TEST_P(FilterIntegrationTest, OverflowDecoderBufferFromDecodeData) {
  config_helper_.setBufferLimits(64 * 1024, 64 * 1024);
  prependFilter(R"EOF(
  name: crash-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.CrashFilterConfig
      crash_in_decode_headers: true
      crash_in_decode_data: true
  )EOF");
  // Buffer filter causes filter manager to buffer data
  prependFilter(R"EOF(
  name: encoder-decoder-buffer-filter
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":scheme", "http"},
                                                                 {":path", "/test/long/url"},
                                                                 {":authority", "sni.lyft.com"}});
  auto request_encoder = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  // This much data should overflow request buffer in filter manager
  codec_client_->sendData(*request_encoder, 16 * 1024, false);
  codec_client_->sendData(*request_encoder, 64 * 1024, false);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("413", response->headers().getStatusValue());
}

// Verify that when a filter decodeData callback overflows request buffer in filter manager the
// filter chain is aborted and 413 is sent to the client. In this test the overflow occurs after
// filter chain iteration was restarted. It is very similar to the test case above but some filter
// manager's internal state is slightly different.
TEST_P(FilterIntegrationTest, OverflowDecoderBufferFromDecodeDataContinueIteration) {
  config_helper_.setBufferLimits(64 * 1024, 64 * 1024);
  prependFilter(R"EOF(
  name: crash-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.CrashFilterConfig
      crash_in_decode_headers: false
      crash_in_decode_data: true
  )EOF");
  prependFilter(R"EOF(
  name: add-body-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.AddBodyFilterConfig
      where_to_add_body: DECODE_DATA
      body_size: 70000
  )EOF");
  prependFilter(R"EOF(
  name: encoder-decoder-buffer-filter
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":scheme", "http"},
                                                                 {":path", "/test/long/url"},
                                                                 {":authority", "sni.lyft.com"}});
  auto request_encoder = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  // This should cause some data to be buffered without overflowing request buffer.
  codec_client_->sendData(*request_encoder, 16 * 1024, false);
  // The buffer filter will resume filter chain iteration and the next add-body-filter filter
  // will overflow the request buffer.
  codec_client_->sendData(*request_encoder, 16 * 1024, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("413", response->headers().getStatusValue());
}

// Adding data in decodeTrailers without any data in the filter manager's request buffer should work
// as it will overflow the pending_recv_data_ which will cause downstream window updates to stop.
TEST_P(FilterIntegrationTest, OverflowDecoderBufferFromDecodeTrailersWithContinuedIteration) {
  if (downstreamProtocol() == Http::CodecType::HTTP1) {
    return;
  }
  config_helper_.setBufferLimits(64 * 1024, 64 * 1024);
  prependFilter(R"EOF(
  name: add-body-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.AddBodyFilterConfig
      where_to_add_body: DECODE_TRAILERS
      body_size: 70000
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":scheme", "http"},
                                                                 {":path", "/test/long/url"},
                                                                 {":authority", "sni.lyft.com"}});
  auto request_encoder = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  codec_client_->sendData(*request_encoder, 1024, false);
  codec_client_->sendData(*request_encoder, 1024, false);

  if (std::get<0>(GetParam()).http2_implementation == Http2Impl::Oghttp2) {
    EXPECT_LOG_NOT_CONTAINS(
        "error", "DataFrameSource will send fin, preventing trailers",
        codec_client_->sendTrailers(*request_encoder,
                                    Http::TestRequestTrailerMapImpl{{"some", "trailer"}}));
  } else {
    codec_client_->sendTrailers(*request_encoder,
                                Http::TestRequestTrailerMapImpl{{"some", "trailer"}});
  }

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Adding data in decodeTrailers with some data in the filter manager's request buffer should case
// 413 as it will overflow the request buffer in filter manager.
TEST_P(FilterIntegrationTest, OverflowDecoderBufferFromDecodeTrailers) {
  if (downstreamProtocol() == Http::CodecType::HTTP1) {
    return;
  }
  config_helper_.setBufferLimits(64 * 1024, 64 * 1024);
  prependFilter(R"EOF(
  name: crash-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.CrashFilterConfig
      crash_in_decode_headers: false
      crash_in_decode_data: true
      crash_in_decode_trailers: true
  )EOF");
  prependFilter(R"EOF(
  name: add-body-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.AddBodyFilterConfig
      where_to_add_body: DECODE_TRAILERS
      where_to_stop_and_buffer: DECODE_DATA
      body_size: 70000
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":scheme", "http"},
                                                                 {":path", "/test/long/url"},
                                                                 {":authority", "sni.lyft.com"}});
  auto request_encoder = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  codec_client_->sendData(*request_encoder, 1024, false);
  codec_client_->sendData(*request_encoder, 1024, false);

  codec_client_->sendTrailers(*request_encoder,
                              Http::TestRequestTrailerMapImpl{{"some", "trailer"}});

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("413", response->headers().getStatusValue());
}

// Verify filters can reset the stream
TEST_P(FilterIntegrationTest, ResetFilter) {
  // Make the add-body-filter stop iteration from encodeData. Headers should be sent to the client.
  prependFilter(R"EOF(
  name: reset-stream-filter
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForReset());
  EXPECT_FALSE(response->complete());
}

// Verify filters can reset the stream
TEST_P(FilterIntegrationTest, EncoderResetFilter) {
  // Make the add-body-filter stop iteration from encodeData. Headers should be sent to the client.
  prependFilter(R"EOF(
  name: encoder-reset-stream-filter
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // Accept request and send response.
  waitForNextUpstreamRequest(0);
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, true);

  // The stream will be in the response path.
  ASSERT_TRUE(response->waitForReset());
  EXPECT_FALSE(response->complete());
}

TEST_P(FilterIntegrationTest, EncoderResetFilterAndContinue) {
  // Make the add-body-filter stop iteration from encodeData. Headers should be sent to the client.
  prependFilter(R"EOF(
  name: encoder-reset-stream-filter
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.addCopy("continue-after-reset", "true");
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // Accept request and send response.
  waitForNextUpstreamRequest(0);
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, true);

  // The stream will be in the response path.
  ASSERT_TRUE(response->waitForReset());
  EXPECT_FALSE(response->complete());
}

TEST_P(FilterIntegrationTest, LocalReplyViaFilterChainDoesNotConcurrentlyInvokeFilter) {
  prependFilter(R"EOF(
  name: assert-non-reentrant-filter
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("AssertNonReentrantFilter local reply during decodeHeaders.", response->body());
}

TEST_P(FilterIntegrationTest, LocalReplyFromDecodeMetadata) {
  prependFilter(R"EOF(
  name: crash-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.CrashFilterConfig
      crash_in_encode_headers: false
      crash_in_encode_data: false
      crash_in_decode_headers: true
      crash_in_decode_data: true
      crash_in_decode_metadata: true
  )EOF");
  prependFilter(R"EOF(
    name: metadata-control-filter
  )EOF");
  autonomous_upstream_ = true;
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.mutable_http2_protocol_options()->set_allow_metadata(true); });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  // Send headers. We expect this to pause.
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  Http::RequestEncoder& encoder = encoder_decoder.first;
  IntegrationStreamDecoderPtr& decoder = encoder_decoder.second;
  codec_client_->sendData(encoder, "abc", false);
  Http::MetadataMap metadata;
  metadata["local_reply"] = "true";
  codec_client_->sendMetadata(encoder, metadata);

  ASSERT_TRUE(decoder->waitForEndStream());

  EXPECT_EQ("400", decoder->headers().getStatusValue());
}

TEST_P(FilterIntegrationTest, LocalReplyFromEncodeMetadata) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() >= 1, "");
    ConfigHelper::HttpProtocolOptions protocol_options;
    protocol_options.mutable_explicit_http_config()
        ->mutable_http2_protocol_options()
        ->set_allow_metadata(true);
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });

  prependFilter(R"EOF(
  name: metadata-control-filter
  )EOF");
  autonomous_upstream_ = false;
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  Http::MetadataMap metadata_map;
  metadata_map["local_reply"] = "true";
  auto metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
  Http::MetadataMapVector metadata_map_vector;
  metadata_map_vector.push_back(std::move(metadata_map_ptr));

  upstream_request_->encodeMetadata(metadata_map_vector);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("400", response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
}

// Validate that adding trailers during encode/decodeData with end_stream==true works correctly
// with half close enabled.
TEST_P(FilterIntegrationTest, FilterAddsTrailersWithIndependentHalfClose) {
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.allow_multiplexed_upstream_half_close", "true");
  prependFilter(R"EOF(
  name: add-trailers-filter
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":scheme", "http"},
                                                                 {":path", "/test/long/url"},
                                                                 {":authority", "sni.lyft.com"}});
  auto request_encoder = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  // The add-body-filter will add trailers to the response
  upstream_request_->encodeData(100, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(response->body().size(), 100);
  // Make sure response trailers added by the filter were received by the client
  EXPECT_EQ(response->trailers()->size(), 1);

  Buffer::OwnedImpl data(std::string(100, 'r'));
  codec_client_->sendData(*request_encoder, data, true);

  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  ASSERT_TRUE(upstream_request_->complete());
  // Make sure request trailers added by the filter were received by the upstream
  EXPECT_EQ(upstream_request_->trailers()->size(), 1);
  EXPECT_EQ(upstream_request_->bodyLength(), 100);
}

void FilterIntegrationTest::testNonTerminalEncodingFilterWithIncompleteRequest() {
  // Encoding by non-terminal upstream filter is not supported
  if (!testing_downstream_filter_) {
    return;
  }
  prependFilter(R"EOF(
  name: non-terminal-encoding-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.NonTerminalEncodingFilterConfig
      where_to_start_encoding: DECODE_HEADERS
      encode_body: IN_TIMER_CALLBACK
      encode_trailers: IN_TIMER_CALLBACK
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":scheme", "http"},
                                                                 {":path", "/test/long/url"},
                                                                 {":authority", "sni.lyft.com"}});
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(response->waitForReset());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(response->body(), "encoded body");
  EXPECT_EQ(response->trailers()->size(), 1);
}

// Verify that when non terminal filter encodes end_stream with request still incomplete
// the stream is reset. The behavior is the same with or without independent half-close enabled.
TEST_P(FilterIntegrationTest, NonTerminalEncodingFilterWithIncompleteRequest) {
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.allow_multiplexed_upstream_half_close", "false");
  testNonTerminalEncodingFilterWithIncompleteRequest();
}

TEST_P(FilterIntegrationTest,
       NonTerminalEncodingFilterWithIncompleteRequestAndIdependentHalfClose) {
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.allow_multiplexed_upstream_half_close", "true");
  testNonTerminalEncodingFilterWithIncompleteRequest();
}

void FilterIntegrationTest::testNonTerminalEncodingFilterWithCompleteRequest() {
  // Encoding by non-terminal upstream filter is not supported
  if (!testing_downstream_filter_) {
    return;
  }
  prependFilter(R"EOF(
  name: non-terminal-encoding-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.NonTerminalEncodingFilterConfig
      where_to_start_encoding: DECODE_DATA
      encode_body: SYNCHRONOUSLY
      encode_trailers: IN_TIMER_CALLBACK
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":scheme", "http"},
                                                                 {":path", "/test/long/url"},
                                                                 {":authority", "sni.lyft.com"}});
  auto request_encoder = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  Buffer::OwnedImpl data(std::string(100, 'r'));
  // Complete request and kick off response encoding from decodeData
  codec_client_->sendData(*request_encoder, data, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_FALSE(response->reset());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(response->body(), "encoded body");
  EXPECT_EQ(response->trailers()->size(), 1);
}

// Verify that when non terminal filter encodes end_stream with request complete
// the stream is finished gracefully and not reset. The behavior is the same with or without
// independent half-close enabled.
TEST_P(FilterIntegrationTest, NonTerminalEncodingFilterWithCompleteRequest) {
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.allow_multiplexed_upstream_half_close", "false");
  testNonTerminalEncodingFilterWithCompleteRequest();
}

TEST_P(FilterIntegrationTest, NonTerminalEncodingFilterWithCompleteRequestAndIdependentHalfClose) {
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.allow_multiplexed_upstream_half_close", "true");
  testNonTerminalEncodingFilterWithCompleteRequest();
}

void FilterIntegrationTest::testFilterAddsDataAndTrailersToHeaderOnlyRequest() {
  // When an upstream filter adds body to the header only request the result observed by
  // the upstream server is unpredictable. Sending of the added body races with the
  // downstream FM closing the request, as it observed end_stream in both directions.
  // This use case is effectively unsupported at this point.
  if (!testing_downstream_filter_) {
    return;
  }
  prependFilter(R"EOF(
  name: add-trailers-filter
  )EOF");
  prependFilter(R"EOF(
  name: add-body-filter-2
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.AddBodyFilterConfig
      where_to_add_body: ENCODE_HEADERS
      body_size: 100
      where_to_stop_and_buffer: DECODE_DATA
  )EOF");
  prependFilter(R"EOF(
  name: add-body-filter-1
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.AddBodyFilterConfig
      where_to_add_body: DECODE_HEADERS
      body_size: 100
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  // Even though client's request had only headers, the add-body-filter will add body and pause
  // filter chain in decodeData, so the upstream should see incomplete request.
  ASSERT_FALSE(upstream_request_->complete());

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  // Make sure response body added by the filter were received by the client
  EXPECT_EQ(response->body().size(), 100);
  // Note that client will not see reset because Envoy's downstream codec does not
  // send RST_STREAM after it observed END_STREAM in both directions.
  EXPECT_FALSE(response->reset());

  if (testing_downstream_filter_) {
    // When response is complete the downstream FM will reset the upstream request that was made
    // incomplete by adding data and then pausing decoder filter chain.
    ASSERT_TRUE(upstream_request_->waitForReset());
  } else {
    // The upstream response completes successfully if add-body-filter is an upstream filter.
    // This is because the downstream HCM sees completed stream and does not issue reset.
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  }
}

// Validate that extending request lifetime by adding body and trailers to a header only request
// and then pausing decoder filter chain, results in stream reset when encoding completes.
// The behavior is the same with or without independent half-close enabled.
TEST_P(FilterIntegrationTest, FilterAddsDataToHeaderOnlyRequest) {
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.allow_multiplexed_upstream_half_close", "false");
  testFilterAddsDataAndTrailersToHeaderOnlyRequest();
}

TEST_P(FilterIntegrationTest, FilterAddsDataToHeaderOnlyRequestWithIndependentHalfClose) {
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.allow_multiplexed_upstream_half_close", "true");
  testFilterAddsDataAndTrailersToHeaderOnlyRequest();
}

// Add metadata in the first filter before recreate the stream in the second filter,
// on response path.
TEST_P(FilterIntegrationTest, RecreateStreamAfterEncodeMetadata) {
  // recreateStream is not supported in Upstream filter chain.
  if (!testing_downstream_filter_) {
    return;
  }

  prependFilter("{ name: add-metadata-encode-headers-filter }");
  prependFilter("{ name: encoder-recreate-stream-filter }");
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.mutable_http2_protocol_options()->set_allow_metadata(true); });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);

  // Second upstream request is triggered by recreateStream.
  FakeStreamPtr upstream_request_2;
  // Wait for the next stream on the upstream connection.
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_2));
  // Wait for the stream to be completely received.
  ASSERT_TRUE(upstream_request_2->waitForEndStream(*dispatcher_));
  upstream_request_2->encodeHeaders(default_response_headers_, true);

  // Wait for the response to be completely received.
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());

  // Verify the metadata is received.
  std::set<std::string> expected_metadata_keys = {"headers", "duplicate"};
  EXPECT_EQ(response->metadataMap().size(), expected_metadata_keys.size());
  for (const auto& key : expected_metadata_keys) {
    // keys are the same as their corresponding values.
    auto it = response->metadataMap().find(key);
    ASSERT_FALSE(it == response->metadataMap().end()) << "key: " << key;
    EXPECT_EQ(response->metadataMap().find(key)->second, key);
  }
}

// Add metadata in the first filter on local reply path.
TEST_P(FilterIntegrationTest, EncodeMetadataOnLocalReply) {
  // Local replies are not seen by upstream HTTP filters. add-metadata-encode-headers-filter will
  // not be invoked if it is installed in upstream filter chain.
  // Thus, this test is only applicable to downstream filter chain.
  if (!testing_downstream_filter_) {
    return;
  }

  prependFilter("{ name: local-reply-during-decode }");
  prependFilter("{ name: add-metadata-encode-headers-filter }");

  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.mutable_http2_protocol_options()->set_allow_metadata(true); });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("500", response->headers().getStatusValue());

  // Verify the metadata is received.
  std::set<std::string> expected_metadata_keys = {"headers", "duplicate"};
  EXPECT_EQ(response->metadataMap().size(), expected_metadata_keys.size());
  for (const auto& key : expected_metadata_keys) {
    // keys are the same as their corresponding values.
    auto it = response->metadataMap().find(key);
    ASSERT_FALSE(it == response->metadataMap().end()) << "key: " << key;
    EXPECT_EQ(response->metadataMap().find(key)->second, key);
  }
}

} // namespace
} // namespace Envoy
