#include <iostream>
#include <memory>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/http/header_map_impl.h"

#include "test/integration/autonomous_upstream.h"
#include "test/integration/http_protocol_integration.h"
#include "test/test_common/printers.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

class MultiplexedUpstreamIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override {
    upstream_tls_ = true;
    config_helper_.configureUpstreamTls(use_alpn_, upstreamProtocol() == Http::CodecType::HTTP3);
    HttpProtocolIntegrationTest::initialize();
  }

  void bidirectionalStreaming(uint32_t bytes);
  void manySimultaneousRequests(uint32_t request_bytes, uint32_t max_response_bytes,
                                uint32_t num_streams = 50);

  bool use_alpn_{false};

  uint64_t upstreamRxResetCounterValue();
  uint64_t upstreamTxResetCounterValue();
  uint64_t downstreamRxResetCounterValue();
  uint64_t downstreamTxResetCounterValue();
};

INSTANTIATE_TEST_SUITE_P(Protocols, MultiplexedUpstreamIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP2},
                             {Http::CodecType::HTTP2, Http::CodecType::HTTP3})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(MultiplexedUpstreamIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, false);
}

TEST_P(MultiplexedUpstreamIntegrationTest, RouterRequestAndResponseWithZeroByteBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(0, 0, false);
}

TEST_P(MultiplexedUpstreamIntegrationTest, RouterHeaderOnlyRequestAndResponseNoBuffer) {
  testRouterHeaderOnlyRequestAndResponse();
}

TEST_P(MultiplexedUpstreamIntegrationTest, RouterUpstreamDisconnectBeforeRequestcomplete) {
  testRouterUpstreamDisconnectBeforeRequestComplete();
}

TEST_P(MultiplexedUpstreamIntegrationTest, RouterUpstreamDisconnectBeforeResponseComplete) {
  testRouterUpstreamDisconnectBeforeResponseComplete();
}

TEST_P(MultiplexedUpstreamIntegrationTest, RouterDownstreamDisconnectBeforeRequestComplete) {
  testRouterDownstreamDisconnectBeforeRequestComplete();

  // Given the downstream disconnect, Envoy will reset the upstream stream.
  EXPECT_EQ(1, upstreamTxResetCounterValue());
  EXPECT_EQ(0, upstreamRxResetCounterValue());
}

TEST_P(MultiplexedUpstreamIntegrationTest, RouterDownstreamDisconnectBeforeResponseComplete) {
  testRouterDownstreamDisconnectBeforeResponseComplete();
}

TEST_P(MultiplexedUpstreamIntegrationTest, RouterUpstreamResponseBeforeRequestComplete) {
  testRouterUpstreamResponseBeforeRequestComplete();
}

TEST_P(MultiplexedUpstreamIntegrationTest, Retry) { testRetry(); }

TEST_P(MultiplexedUpstreamIntegrationTest, GrpcRetry) { testGrpcRetry(); }

TEST_P(MultiplexedUpstreamIntegrationTest, Trailers) { testTrailers(1024, 2048, true, true); }

TEST_P(MultiplexedUpstreamIntegrationTest, RouterRequestAndResponseWithTcpKeepalive) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto keepalive = bootstrap.mutable_static_resources()
                         ->mutable_clusters(0)
                         ->mutable_upstream_connection_options()
                         ->mutable_tcp_keepalive();
    keepalive->mutable_keepalive_probes()->set_value(4);
    keepalive->mutable_keepalive_time()->set_value(7);
    keepalive->mutable_keepalive_interval()->set_value(1);
  });
  testRouterRequestAndResponseWithBody(1024, 512, false);
}

TEST_P(MultiplexedUpstreamIntegrationTest, TestSchemeAndXFP) {
  autonomous_upstream_ = true;
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto check_preserved = ([&](absl::string_view scheme, absl::string_view xff) {
    {
      default_request_headers_.setScheme(scheme);
      default_request_headers_.setForwardedProto(xff);
      auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
      ASSERT_TRUE(response->waitForEndStream());
      auto headers = reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())
                         ->lastRequestHeaders();
      // Ensure original scheme and x-forwarded-proto are preserved.
      EXPECT_EQ(headers->getSchemeValue(), scheme);
      EXPECT_EQ(headers->getForwardedProtoValue(), xff);
    }
  });

  // Ensure regardless of value, scheme and x-forwarded-proto are independently preserved.
  check_preserved("http", "https");
  check_preserved("https", "http");

  codec_client_->close();
}

// Ensure Envoy handles streaming requests and responses simultaneously.
void MultiplexedUpstreamIntegrationTest::bidirectionalStreaming(uint32_t bytes) {
  config_helper_.prependFilter(fmt::format(R"EOF(
  name: stream-info-to-headers-filter
)EOF"));

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "sni.lyft.com"}});
  auto response = std::move(encoder_decoder.second);
  request_encoder_ = &encoder_decoder.first;
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  // Send part of the request body and ensure it is received upstream.
  codec_client_->sendData(*request_encoder_, bytes, false);
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, bytes));

  // Start sending the response and ensure it is received downstream.
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(bytes, false);
  response->waitForBodyData(bytes);

  // Finish the request.
  codec_client_->sendTrailers(*request_encoder_,
                              Http::TestRequestTrailerMapImpl{{"trailer", "foo"}});
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Finish the response.
  upstream_request_->encodeTrailers(Http::TestResponseTrailerMapImpl{{"trailer", "bar"}});
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  std::string expected_alpn = upstreamProtocol() == Http::CodecType::HTTP2 ? "h2" : "h3";
  ASSERT_FALSE(response->headers().get(Http::LowerCaseString("alpn")).empty());
  ASSERT_EQ(response->headers().get(Http::LowerCaseString("alpn"))[0]->value().getStringView(),
            expected_alpn);

  ASSERT_FALSE(response->headers().get(Http::LowerCaseString("upstream_connect_start")).empty());
  ASSERT_FALSE(response->headers().get(Http::LowerCaseString("upstream_connect_complete")).empty());

  ASSERT_FALSE(response->headers().get(Http::LowerCaseString("num_streams")).empty());
  EXPECT_EQ(
      "1",
      response->headers().get(Http::LowerCaseString("num_streams"))[0]->value().getStringView());
  EXPECT_EQ(
      fake_upstreams_[0]->localAddress()->asString(),
      response->headers().get(Http::LowerCaseString("remote_address"))[0]->value().getStringView());
}

TEST_P(MultiplexedUpstreamIntegrationTest, BidirectionalStreaming) { bidirectionalStreaming(1024); }

TEST_P(MultiplexedUpstreamIntegrationTest, LargeBidirectionalStreamingWithBufferLimits) {
  config_helper_.setBufferLimits(1024, 1024); // Set buffer limits upstream and downstream.
  bidirectionalStreaming(1024 * 32);
}

uint64_t MultiplexedUpstreamIntegrationTest::upstreamRxResetCounterValue() {
  return test_server_
      ->counter(absl::StrCat("cluster.cluster_0.", upstreamProtocolStatsRoot(), ".rx_reset"))
      ->value();
}

uint64_t MultiplexedUpstreamIntegrationTest::upstreamTxResetCounterValue() {
  return test_server_
      ->counter(absl::StrCat("cluster.cluster_0.", upstreamProtocolStatsRoot(), ".tx_reset"))
      ->value();
}

uint64_t MultiplexedUpstreamIntegrationTest::downstreamRxResetCounterValue() {
  return test_server_->counter(absl::StrCat(downstreamProtocolStatsRoot(), ".rx_reset"))->value();
}
uint64_t MultiplexedUpstreamIntegrationTest::downstreamTxResetCounterValue() {
  return test_server_->counter(absl::StrCat(downstreamProtocolStatsRoot(), ".tx_reset"))->value();
}

TEST_P(MultiplexedUpstreamIntegrationTest, BidirectionalStreamingReset) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start sending the request.
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "sni.lyft.com"}});
  auto response = std::move(encoder_decoder.second);
  request_encoder_ = &encoder_decoder.first;
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  // Send some request data.
  codec_client_->sendData(*request_encoder_, 1024, false);
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 1024));

  // Start sending the response.
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(1024, false);
  response->waitForBodyData(1024);

  // Finish sending the request.
  codec_client_->sendTrailers(*request_encoder_,
                              Http::TestRequestTrailerMapImpl{{"trailer", "foo"}});
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Reset the stream.
  upstream_request_->encodeResetStream();
  ASSERT_TRUE(response->waitForReset());
  EXPECT_FALSE(response->complete());

  // The upstream stats should reflect receiving the reset, and downstream
  // reflect sending it on.
  EXPECT_EQ(1, upstreamRxResetCounterValue());
  EXPECT_EQ(0, upstreamTxResetCounterValue());
  EXPECT_EQ(0, downstreamRxResetCounterValue());
  EXPECT_EQ(1, downstreamTxResetCounterValue());
}

TEST_P(MultiplexedUpstreamIntegrationTest, SimultaneousRequest) {
  simultaneousRequest(1024, 512, 1023, 513);
}

TEST_P(MultiplexedUpstreamIntegrationTest, LargeSimultaneousRequestWithBufferLimits) {
  config_helper_.setBufferLimits(1024, 1024); // Set buffer limits upstream and downstream.
  simultaneousRequest(1024 * 20, 1024 * 14 + 2, 1024 * 10 + 5, 1024 * 16);
}

void MultiplexedUpstreamIntegrationTest::manySimultaneousRequests(uint32_t request_bytes,
                                                                  uint32_t max_response_bytes,
                                                                  uint32_t num_requests) {
  TestRandomGenerator rand;
  std::vector<Http::RequestEncoder*> encoders;
  std::vector<IntegrationStreamDecoderPtr> responses;
  std::vector<int> response_bytes;
  autonomous_upstream_ = true;
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  for (uint32_t i = 0; i < num_requests; ++i) {
    response_bytes.push_back(rand.random() % (max_response_bytes));
    auto headers = Http::TestRequestHeaderMapImpl{
        {":method", "POST"},
        {":path", "/test/long/url"},
        {":scheme", "http"},
        {":authority", "sni.lyft.com"},
        {AutonomousStream::RESPONSE_SIZE_BYTES, std::to_string(response_bytes[i])},
        {AutonomousStream::EXPECT_REQUEST_SIZE_BYTES, std::to_string(request_bytes)}};
    if (i % 2 == 0) {
      headers.addCopy(AutonomousStream::RESET_AFTER_REQUEST, "yes");
    }
    auto encoder_decoder = codec_client_->startRequest(headers);
    encoders.push_back(&encoder_decoder.first);
    responses.push_back(std::move(encoder_decoder.second));
    codec_client_->sendData(*encoders[i], request_bytes, true);
  }

  for (uint32_t i = 0; i < num_requests; ++i) {
    ASSERT_TRUE(responses[i]->waitForEndStream());
    if (i % 2 != 0) {
      EXPECT_TRUE(responses[i]->complete());
      EXPECT_EQ("200", responses[i]->headers().getStatusValue());
      EXPECT_EQ(response_bytes[i], responses[i]->body().length());
    } else {
      // Upstream stream reset.
      EXPECT_EQ("503", responses[i]->headers().getStatusValue());
    }
  }

  EXPECT_EQ(0, test_server_->gauge("http2.streams_active")->value());
  EXPECT_EQ(0, test_server_->gauge("http2.pending_send_bytes")->value());
}

TEST_P(MultiplexedUpstreamIntegrationTest, ManySimultaneousRequest) {
  manySimultaneousRequests(1024, 1024, 100);
}

TEST_P(MultiplexedUpstreamIntegrationTest, TooManySimultaneousRequests) {
  manySimultaneousRequests(1024, 1024, 200);
}

TEST_P(MultiplexedUpstreamIntegrationTest, ManySimultaneousRequestsTightUpstreamLimits) {
  if (upstreamProtocol() == Http::CodecType::HTTP2) {
    return;
  }
  envoy::config::core::v3::Http2ProtocolOptions config;
  config.mutable_max_concurrent_streams()->set_value(1);
  mergeOptions(config);
  envoy::config::listener::v3::QuicProtocolOptions options;
  options.mutable_quic_protocol_options()->mutable_max_concurrent_streams()->set_value(1);
  mergeOptions(options);

  manySimultaneousRequests(1024, 1024, 10);
}

TEST_P(MultiplexedUpstreamIntegrationTest, ManySimultaneousRequestsLaxUpstreamLimits) {
  envoy::config::core::v3::Http2ProtocolOptions config;
  config.mutable_max_concurrent_streams()->set_value(10000);
  mergeOptions(config);
  envoy::config::listener::v3::QuicProtocolOptions options;
  options.mutable_quic_protocol_options()->mutable_max_concurrent_streams()->set_value(10000);
  mergeOptions(options);

  if (upstreamProtocol() == Http::CodecType::HTTP3) {
    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() >= 1, "");
          ConfigHelper::HttpProtocolOptions protocol_options;
          protocol_options.mutable_explicit_http_config()
              ->mutable_http3_protocol_options()
              ->mutable_quic_protocol_options()
              ->mutable_max_concurrent_streams()
              ->set_value(10000);
          ConfigHelper::setProtocolOptions(
              *bootstrap.mutable_static_resources()->mutable_clusters(0), protocol_options);
        });
  }

  manySimultaneousRequests(1024, 1024, 10);
}

TEST_P(MultiplexedUpstreamIntegrationTest, ManyLargeSimultaneousRequestWithBufferLimits) {
  config_helper_.setBufferLimits(1024, 1024); // Set buffer limits upstream and downstream.
  manySimultaneousRequests(1024 * 20, 1024 * 20);
}

TEST_P(MultiplexedUpstreamIntegrationTest, ManyLargeSimultaneousRequestWithRandomBackup) {
  // random-pause-filter does not support HTTP3.
  if (upstreamProtocol() == Http::CodecType::HTTP3) {
    return;
  }

  if (GetParam().defer_processing_backedup_streams) {
    // TODO(kbaichoo): fix this test to work with deferred processing by using a
    // timer to lower the watermark when the filter has raised above watermark.
    // Since we deferred processing data, when the filter raises watermark
    // with deferred processing we won't invoke it again which could lower
    // the watermark.
    return;
  }
  config_helper_.prependFilter(R"EOF(
  name: random-pause-filter
)EOF");

  manySimultaneousRequests(1024 * 20, 1024 * 20);
}

TEST_P(MultiplexedUpstreamIntegrationTest, UpstreamConnectionCloseWithManyStreams) {
  config_helper_.setBufferLimits(1024, 1024); // Set buffer limits upstream and downstream.
  const uint32_t num_requests = 20;
  std::vector<Http::RequestEncoder*> encoders;
  std::vector<IntegrationStreamDecoderPtr> responses;
  std::vector<FakeStreamPtr> upstream_requests;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  for (uint32_t i = 0; i < num_requests; ++i) {
    auto encoder_decoder =
        codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                   {":path", "/test/long/url"},
                                                                   {":scheme", "http"},
                                                                   {":authority", "sni.lyft.com"}});
    encoders.push_back(&encoder_decoder.first);
    responses.push_back(std::move(encoder_decoder.second));

    // Ensure that we establish the first request (which will be reset) to avoid
    // a race where the reset is detected before the upstream stream is
    // established (#5316)
    if (i == 0) {
      ASSERT_TRUE(
          fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
      upstream_requests.emplace_back();
      ASSERT_TRUE(
          fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_requests.back()));
    }

    if (i != 0) {
      codec_client_->sendData(*encoders[i], 0, true);
    }
  }

  // Reset one stream to test how reset and watermarks interact.
  codec_client_->sendReset(*encoders[0]);

  // Now drain the upstream connection.
  for (uint32_t i = 1; i < num_requests; ++i) {
    upstream_requests.emplace_back();
    ASSERT_TRUE(
        fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_requests.back()));
  }
  for (uint32_t i = 1; i < num_requests; ++i) {
    ASSERT_TRUE(upstream_requests[i]->waitForEndStream(*dispatcher_));
    upstream_requests[i]->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    upstream_requests[i]->encodeData(100, false);
    // Make sure at least the headers go through, to ensure stream reset rather
    // than disconnect.
    responses[i]->waitForHeaders();
  }
  // Close the connection.
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  // Ensure the streams are all reset successfully.
  for (uint32_t i = 1; i < num_requests; ++i) {
    ASSERT_TRUE(responses[i]->waitForReset());
  }

  EXPECT_NE(0, downstreamRxResetCounterValue());
}

// Regression test for https://github.com/envoyproxy/envoy/issues/6744
TEST_P(MultiplexedUpstreamIntegrationTest, HittingEncoderFilterLimitForGrpc) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        const std::string access_log_name =
            TestEnvironment::temporaryPath(TestUtility::uniqueFilename());
        // Configure just enough of an upstream access log to reference the upstream headers.
        const std::string yaml_string = fmt::format(R"EOF(
name: router
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
  upstream_log:
    name: accesslog
    filter:
      not_health_check_filter: {{}}
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: {}
  )EOF",
                                                    Platform::null_device_path);
        TestUtility::loadFromYaml(yaml_string, *hcm.mutable_http_filters(1));
      });

  // As with ProtocolIntegrationTest.HittingEncoderFilterLimit use a filter
  // which buffers response data but in this case, make sure the sendLocalReply
  // is gRPC.
  config_helper_.prependFilter("{ name: encoder-decoder-buffer-filter }");
  config_helper_.setBufferLimits(1024, 1024);
  initialize();

  // Send the request.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "sni.lyft.com"},
                                                                 {"te", "trailers"}});
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
  ASSERT_TRUE(upstream_request_->waitForReset());
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());

  // As the error was internal, Envoy should reset the upstream connection.
  // Downstream gets an error, so no resets there.
  EXPECT_EQ(1, upstreamTxResetCounterValue());
  EXPECT_EQ(0, downstreamTxResetCounterValue());
  EXPECT_EQ(0, upstreamRxResetCounterValue());
  EXPECT_EQ(0, downstreamRxResetCounterValue());
}

// Tests the default limit for the number of response headers is 100. Results in a stream reset if
// exceeds.
TEST_P(MultiplexedUpstreamIntegrationTest, TestManyResponseHeadersRejected) {
  // Default limit for response headers is 100.
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestResponseHeaderMapImpl many_headers(default_response_headers_);
  for (int i = 0; i < 100; i++) {
    many_headers.addCopy(absl::StrCat("many", i), std::string(1, 'a'));
  }
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(many_headers, true);
  ASSERT_TRUE(response->waitForEndStream());
  // Upstream stream reset triggered.
  EXPECT_EQ("503", response->headers().getStatusValue());
}

// Tests bootstrap configuration of max response headers.
TEST_P(MultiplexedUpstreamIntegrationTest, ManyResponseHeadersAccepted) {
  // Set max response header count to 200.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    ConfigHelper::HttpProtocolOptions protocol_options;
    auto* http_protocol_options = protocol_options.mutable_common_http_protocol_options();
    http_protocol_options->mutable_max_headers_count()->set_value(200);
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });
  Http::TestResponseHeaderMapImpl response_headers(default_response_headers_);
  for (int i = 0; i < 150; i++) {
    response_headers.addCopy(std::to_string(i), std::string(1, 'a'));
  }

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(response_headers, false);
  upstream_request_->encodeData(512, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
}

// Tests that HTTP/2 response headers over 60 kB are rejected and result in a stream reset.
TEST_P(MultiplexedUpstreamIntegrationTest, LargeResponseHeadersRejected) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestResponseHeaderMapImpl large_headers(default_response_headers_);
  large_headers.addCopy("large", std::string(60 * 1024, 'a'));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(large_headers, true);
  ASSERT_TRUE(response->waitForEndStream());
  // Upstream stream reset.
  EXPECT_EQ("503", response->headers().getStatusValue());
}

TEST_P(MultiplexedUpstreamIntegrationTest, NoInitialStreams) {
  // Set the fake upstream to start with 0 streams available.
  upstreamConfig().http2_options_.mutable_max_concurrent_streams()->set_value(0);
  EXPECT_EQ(0, upstreamConfig().http2_options_.max_concurrent_streams().value());

  envoy::config::listener::v3::QuicProtocolOptions options;
  options.mutable_quic_protocol_options()->mutable_max_concurrent_streams()->set_value(0);
  mergeOptions(options);
  initialize();

  // Create the client connection and send a request.
  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), absl::nullopt);
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "5xx"},
                                     {"x-envoy-upstream-rq-per-try-timeout-ms", "100"},
                                     {"x-envoy-max-retries", "100"}});

  // There should now be an upstream connection, but no upstream stream.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_FALSE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_,
                                                           std::chrono::milliseconds(100)));

  // Update the upstream to have 1 stream available. Now Envoy should ship the
  // original request upstream.
  fake_upstream_connection_->updateConcurrentStreams(1);
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  // Make sure the standard request/response pipeline works as expected.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Regression test for https://github.com/envoyproxy/envoy/issues/13933
TEST_P(MultiplexedUpstreamIntegrationTest, MultipleRequestsLowStreamLimit) {
  autonomous_upstream_ = true;
  envoy::config::core::v3::Http2ProtocolOptions config;
  config.mutable_max_concurrent_streams()->set_value(1);
  mergeOptions(config);

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start sending the request, but ensure no end stream will be sent, so the
  // stream will stay in use.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {AutonomousStream::NO_END_STREAM, "true"}});
  // Wait until the response is sent to ensure the SETTINGS frame has been read
  // by Envoy.
  response->waitForHeaders();
  ASSERT_FALSE(response->complete());

  // Now send a second request and make sure it is processed. Previously it
  // would be queued on the original connection, as Envoy would ignore the
  // peer's SETTINGS frame and nghttp2 would then queue it, but now it should
  // result in a second connection and an immediate response.
  FakeStreamPtr upstream_request2;
  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response2->waitForEndStream());
  cleanupUpstreamAndDownstream();
}

// Regression test for https://github.com/envoyproxy/envoy/issues/13933
TEST_P(MultiplexedUpstreamIntegrationTest, UpstreamGoaway) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Kick off the initial request and make sure it's received upstream.
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Send a goaway from upstream.
  fake_upstream_connection_->encodeGoAway();
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_close_notify", 1);

  // A new request should result in a new connection
  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  FakeHttpConnectionPtr fake_upstream_connection2;
  FakeStreamPtr upstream_request2;
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection2));
  ASSERT_TRUE(fake_upstream_connection2->waitForNewStream(*dispatcher_, upstream_request2));

  // Clean up
  ASSERT_TRUE(fake_upstream_connection2->close());
  ASSERT_TRUE(fake_upstream_connection2->waitForDisconnect());
  fake_upstream_connection2.reset();
  cleanupUpstreamAndDownstream();
}

TEST_P(MultiplexedUpstreamIntegrationTest, AutoRetrySafeRequestUponTooEarlyResponse) {
  if (!Runtime::runtimeFeatureEnabled(Runtime::conn_pool_new_stream_with_early_data_and_http3)) {
    return;
  }

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Kick off the initial request and make sure it's received upstream.
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  const Http::TestResponseHeaderMapImpl too_early_response_headers{{":status", "425"}};
  upstream_request_->encodeHeaders(too_early_response_headers, true);
  if (upstreamProtocol() == Http::CodecType::HTTP2) {
    ASSERT_TRUE(response->waitForEndStream());
    // 425 response should be forwarded back to the client as HTTP/2 upstream doesn't support early
    // data.
    EXPECT_EQ("425", response->headers().getStatusValue());
    return;
  }
  // 425 response will be retried by Envoy, so expect another upstream request.
  waitForNextUpstreamRequest(0);
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_retry")->value());
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());

  Http::TestRequestHeaderMapImpl request{{":method", "GET"},
                                         {":path", "/test/long/url"},
                                         {":scheme", "http"},
                                         {":authority", "sni.lyft.com"},
                                         {"Early-Data", "1"}};
  auto response2 = codec_client_->makeHeaderOnlyRequest(request);
  waitForNextUpstreamRequest(0);
  // If the request already has Early-Data header, no additional Early-Data header should be added
  // and the header should be forwarded as is.
  EXPECT_THAT(upstream_request_->headers(), HeaderValueOf(Http::Headers::get().EarlyData, "1"));
  upstream_request_->encodeHeaders(too_early_response_headers, true);
  ASSERT_TRUE(response2->waitForEndStream());
  // 425 response should be forwarded back to the client.
  EXPECT_EQ("425", response2->headers().getStatusValue());

  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_retry")->value());
}

TEST_P(MultiplexedUpstreamIntegrationTest, DefaultAllowsUpstreamSafeRequestsUsingEarlyData) {
#ifdef WIN32
  // TODO: debug why waiting on the 2nd upstream connection times out on Windows.
  GTEST_SKIP() << "Skipping on Windows";
#endif
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  upstream_request_.reset();

  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  fake_upstream_connection_.reset();
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_destroy", 1);

  EXPECT_EQ(0u, test_server_->counter("cluster.cluster_0.upstream_cx_connect_with_0_rtt")->value());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_destroy", 1);

  default_request_headers_.addCopy("second_request", "1");
  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  if (upstreamProtocol() == Http::CodecType::HTTP3) {
    EXPECT_EQ(1u,
              test_server_->counter("cluster.cluster_0.upstream_cx_connect_with_0_rtt")->value());
    EXPECT_EQ(1u, test_server_->counter("cluster.cluster_0.upstream_rq_0rtt")->value());
  }
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response2->waitForEndStream());
}

TEST_P(MultiplexedUpstreamIntegrationTest, DisableUpstreamEarlyData) {
#ifdef WIN32
  // TODO: debug why waiting on the 2nd upstream connection times out on Windows.
  GTEST_SKIP() << "Skipping on Windows";
#endif
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* early_data_policy = hcm.mutable_route_config()
                                      ->mutable_virtual_hosts(0)
                                      ->mutable_routes(0)
                                      ->mutable_route()
                                      ->mutable_early_data_policy();
        envoy::extensions::early_data::v3::DefaultEarlyDataPolicy config;
        early_data_policy->set_name("envoy.route.early_data_policy.default");
        early_data_policy->mutable_typed_config()->PackFrom(config);
      });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  upstream_request_.reset();

  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  fake_upstream_connection_.reset();
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_destroy", 1);

  EXPECT_EQ(0u, test_server_->counter("cluster.cluster_0.upstream_cx_connect_with_0_rtt")->value());

  default_request_headers_.addCopy("second_request", "1");
  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  EXPECT_EQ(upstreamProtocol() == Http::CodecType::HTTP3 ? 1u : 0u,
            test_server_->counter("cluster.cluster_0.upstream_cx_connect_with_0_rtt")->value());
  EXPECT_EQ(0u, test_server_->counter("cluster.cluster_0.upstream_rq_0rtt")->value());
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response2->waitForEndStream());
}

// Tests that Envoy will automatically retry a GET request sent over early data if the upstream
// rejects it with TooEarly response.
TEST_P(MultiplexedUpstreamIntegrationTest, UpstreamEarlyDataRejected) {
#ifdef WIN32
  // TODO: debug why waiting on the 0-rtt upstream connection times out on Windows.
  GTEST_SKIP() << "Skipping on Windows";
#endif
  if (upstreamProtocol() != Http::CodecType::HTTP3 ||
      !(Runtime::runtimeFeatureEnabled(Runtime::conn_pool_new_stream_with_early_data_and_http3) &&
        Runtime::runtimeFeatureEnabled("envoy.reloadable_features.http3_sends_early_data"))) {
    return;
  }
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  upstream_request_.reset();

  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  fake_upstream_connection_.reset();
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_destroy", 1);

  EXPECT_EQ(0u, test_server_->counter("cluster.cluster_0.upstream_cx_connect_with_0_rtt")->value());

  default_request_headers_.addCopy("second_request", "1");
  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  EXPECT_EQ(1u, test_server_->counter("cluster.cluster_0.upstream_cx_connect_with_0_rtt")->value());
  EXPECT_EQ(1u, test_server_->counter("cluster.cluster_0.upstream_rq_0rtt")->value());
  EXPECT_EQ(0u, test_server_->counter("cluster.cluster_0.upstream_rq_retry")->value());

  // TooEarly response should be retried automatically.
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "425"}}, true);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response2->waitForEndStream());
  // The retry request shouldn't be sent as early data.
  EXPECT_EQ(1u, test_server_->counter("cluster.cluster_0.upstream_rq_0rtt")->value());
  EXPECT_EQ(1u, test_server_->counter("cluster.cluster_0.upstream_rq_retry")->value());
}

#ifdef ENVOY_ENABLE_QUIC
class QuicCustomTlsServerHandshaker : public quic::TlsServerHandshaker {
public:
  QuicCustomTlsServerHandshaker(quic::QuicSession* session,
                                const quic::QuicCryptoServerConfig* crypto_config,
                                bool fail_handshake)
      : quic::TlsServerHandshaker(session, crypto_config), fail_handshake_(fail_handshake) {}

protected:
  ssl_select_cert_result_t EarlySelectCertCallback(const SSL_CLIENT_HELLO* client_hello) override {
    if (fail_handshake_) {
      ENVOY_LOG_MISC(trace, "Override handshaker return value to error");
      return ssl_select_cert_error;
    }
    return quic::TlsServerHandshaker::EarlySelectCertCallback(client_hello);
  }

private:
  bool fail_handshake_;
};

class QuicFailHandshakeCryptoServerStreamFactory
    : public Quic::EnvoyQuicCryptoServerStreamFactoryInterface {
public:
  Envoy::ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }
  std::string name() const override { return "envoy.quic.crypto_stream.server.fail_handshake"; }

  std::unique_ptr<quic::QuicCryptoServerStreamBase> createEnvoyQuicCryptoServerStream(
      const quic::QuicCryptoServerConfig* crypto_config,
      quic::QuicCompressedCertsCache* /*compressed_certs_cache*/, quic::QuicSession* session,
      quic::QuicCryptoServerStreamBase::Helper* /*helper*/,
      Envoy::OptRef<const Envoy::Network::DownstreamTransportSocketFactory>
      /*transport_socket_factory*/,
      Envoy::Event::Dispatcher& /*dispatcher*/) override {
    ASSERT(session->connection()->version().handshake_protocol == quic::PROTOCOL_TLS1_3);
    return std::make_unique<QuicCustomTlsServerHandshaker>(session, crypto_config, fail_handshake_);
  }

  void setFailHandshake(bool fail_handshake) { fail_handshake_ = fail_handshake; }

private:
  bool fail_handshake_;
};

TEST_P(MultiplexedUpstreamIntegrationTest, UpstreamDisconnectDuringEarlyData) {
#ifdef WIN32
  // TODO: debug why waiting on the 0-rtt upstream connection times out on Windows.
  GTEST_SKIP() << "Skipping on Windows";
#endif
  if (upstreamProtocol() != Http::CodecType::HTTP3 ||
      !(Runtime::runtimeFeatureEnabled(Runtime::conn_pool_new_stream_with_early_data_and_http3) &&
        Runtime::runtimeFeatureEnabled("envoy.reloadable_features.http3_sends_early_data"))) {
    return;
  }
  Runtime::maybeSetRuntimeGuard("envoy.reloadable_features.no_extension_lookup_by_name", false);

  // Register and config this factory to control upstream QUIC handshakes.
  QuicFailHandshakeCryptoServerStreamFactory crypto_stream_factory;
  Registry::InjectFactory<Quic::EnvoyQuicCryptoServerStreamFactoryInterface> registered_factory(
      crypto_stream_factory);
  crypto_stream_factory.setFailHandshake(false);

  envoy::config::listener::v3::QuicProtocolOptions options;
  options.mutable_crypto_stream_config()->set_name(
      "envoy.quic.crypto_stream.server.fail_handshake");
  mergeOptions(options);

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  upstream_request_.reset();

  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  fake_upstream_connection_.reset();
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_destroy", 1);

  EXPECT_EQ(0u, test_server_->counter("cluster.cluster_0.upstream_cx_connect_with_0_rtt")->value());

  // Fail the following upstream connecting attempts.
  crypto_stream_factory.setFailHandshake(true);
  default_request_headers_.addCopy("second_request", "1");
  default_request_headers_.addCopy("x-envoy-retry-on", "connect-failure");
  default_request_headers_.addCopy("x-forwarded-for", "10.0.0.1");
  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  // This connection should have failed handshake.
  waitForNextUpstreamConnection(std::vector<uint64_t>{0}, TestUtility::DefaultTimeout,
                                fake_upstream_connection_);

  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_EQ("503", response2->headers().getStatusValue());
  EXPECT_EQ(2u, test_server_->counter("cluster.cluster_0.upstream_cx_connect_with_0_rtt")->value());
  EXPECT_LE(1u, test_server_->counter("cluster.cluster_0.upstream_rq_0rtt")->value());
  EXPECT_EQ(1u, test_server_->counter("cluster.cluster_0.upstream_rq_retry")->value());
}

#endif

TEST_P(MultiplexedUpstreamIntegrationTest, DownstreamDisconnectDuringEarlyData) {
#ifdef WIN32
  // TODO: debug why waiting on the 0-rtt upstream connection times out on Windows.
  GTEST_SKIP() << "Skipping on Windows";
#endif
  if (upstreamProtocol() != Http::CodecType::HTTP3 ||
      !(Runtime::runtimeFeatureEnabled(Runtime::conn_pool_new_stream_with_early_data_and_http3) &&
        Runtime::runtimeFeatureEnabled("envoy.reloadable_features.http3_sends_early_data"))) {
    return;
  }
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  upstream_request_.reset();

  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  fake_upstream_connection_.reset();
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_destroy", 1);

  EXPECT_EQ(0u, test_server_->counter("cluster.cluster_0.upstream_cx_connect_with_0_rtt")->value());

  {
    // Lock up fake upstream so that it won't process handshake.
    absl::MutexLock l(&fake_upstreams_[0]->lock());
    auto response2 = codec_client_->makeHeaderOnlyRequest(
        Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                       {":path", "/test/long/url"},
                                       {":scheme", "http"},
                                       {":authority", "sni.lyft.com"},
                                       {"second-request", "1"}});
    // Even though the fake upstream is not responding, the 2 GET requests should still be forwarded
    // as early data.
    test_server_->waitForCounterEq("cluster.cluster_0.upstream_rq_total", 2);
    EXPECT_EQ(1u,
              test_server_->counter("cluster.cluster_0.upstream_cx_connect_with_0_rtt")->value());
    EXPECT_LE(1u, test_server_->counter("cluster.cluster_0.upstream_rq_0rtt")->value());
    codec_client_->close();
    test_server_->waitForCounterEq("cluster.cluster_0.upstream_rq_tx_reset", 1);
  }
  // Release the upstream lock and finish the handshake.
  waitForNextUpstreamConnection(std::vector<uint64_t>{0}, TestUtility::DefaultTimeout,
                                fake_upstream_connection_);
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

TEST_P(MultiplexedUpstreamIntegrationTest, ConnPoolQueuingNonSafeRequest) {
#ifdef WIN32
  // TODO: debug why waiting on the 0-rtt upstream connection times out on Windows.
  GTEST_SKIP() << "Skipping on Windows";
#endif
  if (upstreamProtocol() != Http::CodecType::HTTP3 ||
      !(Runtime::runtimeFeatureEnabled(Runtime::conn_pool_new_stream_with_early_data_and_http3) &&
        Runtime::runtimeFeatureEnabled("envoy.reloadable_features.http3_sends_early_data"))) {
    return;
  }
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  upstream_request_.reset();

  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  fake_upstream_connection_.reset();
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_destroy", 1);

  EXPECT_EQ(0u, test_server_->counter("cluster.cluster_0.upstream_cx_connect_with_0_rtt")->value());

  IntegrationStreamDecoderPtr response2;
  IntegrationStreamDecoderPtr response3;
  IntegrationStreamDecoderPtr response4;
  {
    // Lock up fake upstream so that it won't process handshake.
    absl::MutexLock l(&fake_upstreams_[0]->lock());
    response2 = codec_client_->makeHeaderOnlyRequest(
        Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                       {":path", "/test/long/url"},
                                       {":scheme", "http"},
                                       {":authority", "sni.lyft.com"},
                                       {"second-request", "1"}});

    response3 = codec_client_->makeHeaderOnlyRequest(
        Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                       {":path", "/test/long/url"},
                                       {":scheme", "http"},
                                       {":authority", "sni.lyft.com"},
                                       {"third-request", "1"}});
    response4 = codec_client_->makeHeaderOnlyRequest(
        Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                       {":path", "/test/long/url"},
                                       {":scheme", "http"},
                                       {":authority", "sni.lyft.com"},
                                       {"forth-request", "1"}});
    // Even though the fake upstream is not responding, the 2 GET requests should still be forwarded
    // as early data.
    test_server_->waitForCounterEq("cluster.cluster_0.upstream_rq_total", 3);
    EXPECT_EQ(1u,
              test_server_->counter("cluster.cluster_0.upstream_cx_connect_with_0_rtt")->value());
    EXPECT_LE(2u, test_server_->counter("cluster.cluster_0.upstream_rq_0rtt")->value());
  }
  // Release the upstream lock and finish the handshake.
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);
  FakeStreamPtr upstream_request3;
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request3));
  upstream_request3->encodeHeaders(default_response_headers_, true);
  FakeStreamPtr upstream_request4;
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request4));
  upstream_request4->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response2->waitForEndStream());
  ASSERT_TRUE(response3->waitForEndStream());
  ASSERT_TRUE(response4->waitForEndStream());
}

} // namespace Envoy
