#include "test/integration/multiplexed_upstream_integration_test.h"

#include <iostream>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "common/http/header_map_impl.h"

#include "test/integration/autonomous_upstream.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

// TODO(#14829) categorize or fix all failures.
#define EXCLUDE_UPSTREAM_HTTP3                                                                     \
  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP3) {                                     \
    return;                                                                                        \
  }

INSTANTIATE_TEST_SUITE_P(Protocols, Http2UpstreamIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecClient::Type::HTTP2}, {FakeHttpConnection::Type::HTTP2})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// TODO(alyssawilk) move #defines into getProtocolTestParams in a follow-up
#ifdef ENVOY_ENABLE_QUIC
INSTANTIATE_TEST_SUITE_P(ProtocolsWithQuic, Http2UpstreamIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecClient::Type::HTTP2}, {FakeHttpConnection::Type::HTTP3})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);
#endif

TEST_P(Http2UpstreamIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, false);
}

TEST_P(Http2UpstreamIntegrationTest, RouterRequestAndResponseWithZeroByteBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(0, 0, false);
}

TEST_P(Http2UpstreamIntegrationTest, RouterHeaderOnlyRequestAndResponseNoBuffer) {
  testRouterHeaderOnlyRequestAndResponse();
}

TEST_P(Http2UpstreamIntegrationTest, RouterUpstreamDisconnectBeforeRequestcomplete) {
  EXCLUDE_UPSTREAM_HTTP3; // Close loop.
  testRouterUpstreamDisconnectBeforeRequestComplete();
}

TEST_P(Http2UpstreamIntegrationTest, RouterUpstreamDisconnectBeforeResponseComplete) {
  EXCLUDE_UPSTREAM_HTTP3; // Close loop.
  testRouterUpstreamDisconnectBeforeResponseComplete();
}

TEST_P(Http2UpstreamIntegrationTest, RouterDownstreamDisconnectBeforeRequestComplete) {
  testRouterDownstreamDisconnectBeforeRequestComplete();
}

TEST_P(Http2UpstreamIntegrationTest, RouterDownstreamDisconnectBeforeResponseComplete) {
  testRouterDownstreamDisconnectBeforeResponseComplete();
}

TEST_P(Http2UpstreamIntegrationTest, RouterUpstreamResponseBeforeRequestComplete) {
  testRouterUpstreamResponseBeforeRequestComplete();
}

TEST_P(Http2UpstreamIntegrationTest, Retry) {
  EXCLUDE_UPSTREAM_HTTP3; // CHECK failed: max_plaintext_size_ (=18) >= PacketSize() (=20)
  testRetry();
}

TEST_P(Http2UpstreamIntegrationTest, GrpcRetry) {
  EXCLUDE_UPSTREAM_HTTP3; // CHECK failed: max_plaintext_size_ (=18) >= PacketSize() (=20)
  testGrpcRetry();
}

TEST_P(Http2UpstreamIntegrationTest, Trailers) {
  EXCLUDE_UPSTREAM_HTTP3; // CHECK failed: max_plaintext_size_ (=18) >= PacketSize() (=20)
  testTrailers(1024, 2048, true, true);
}

TEST_P(Http2UpstreamIntegrationTest, TestSchemeAndXFP) {
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
void Http2UpstreamIntegrationTest::bidirectionalStreaming(uint32_t bytes) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}});
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
}

TEST_P(Http2UpstreamIntegrationTest, BidirectionalStreaming) { bidirectionalStreaming(1024); }

TEST_P(Http2UpstreamIntegrationTest, LargeBidirectionalStreamingWithBufferLimits) {
  config_helper_.setBufferLimits(1024, 1024); // Set buffer limits upstream and downstream.
  bidirectionalStreaming(1024 * 32);
}

TEST_P(Http2UpstreamIntegrationTest, BidirectionalStreamingReset) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start sending the request.
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}});
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
}

void Http2UpstreamIntegrationTest::simultaneousRequest(uint32_t request1_bytes,
                                                       uint32_t request2_bytes,
                                                       uint32_t response1_bytes,
                                                       uint32_t response2_bytes) {
  FakeStreamPtr upstream_request1;
  FakeStreamPtr upstream_request2;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start request 1
  auto encoder_decoder1 =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}});
  Http::RequestEncoder* encoder1 = &encoder_decoder1.first;
  auto response1 = std::move(encoder_decoder1.second);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request1));

  // Start request 2
  auto encoder_decoder2 =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}});
  Http::RequestEncoder* encoder2 = &encoder_decoder2.first;
  auto response2 = std::move(encoder_decoder2.second);
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request2));

  // Finish request 1
  codec_client_->sendData(*encoder1, request1_bytes, true);
  ASSERT_TRUE(upstream_request1->waitForEndStream(*dispatcher_));

  // Finish request 2
  codec_client_->sendData(*encoder2, request2_bytes, true);
  ASSERT_TRUE(upstream_request2->waitForEndStream(*dispatcher_));

  // Respond to request 2
  upstream_request2->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request2->encodeData(response2_bytes, true);
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_TRUE(upstream_request2->complete());
  EXPECT_EQ(request2_bytes, upstream_request2->bodyLength());
  EXPECT_TRUE(response2->complete());
  EXPECT_EQ("200", response2->headers().getStatusValue());
  EXPECT_EQ(response2_bytes, response2->body().size());

  // Respond to request 1
  upstream_request1->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request1->encodeData(response1_bytes, true);
  ASSERT_TRUE(response1->waitForEndStream());
  EXPECT_TRUE(upstream_request1->complete());
  EXPECT_EQ(request1_bytes, upstream_request1->bodyLength());
  EXPECT_TRUE(response1->complete());
  EXPECT_EQ("200", response1->headers().getStatusValue());
  EXPECT_EQ(response1_bytes, response1->body().size());
}

TEST_P(Http2UpstreamIntegrationTest, SimultaneousRequest) {
  simultaneousRequest(1024, 512, 1023, 513);
}

TEST_P(Http2UpstreamIntegrationTest, LargeSimultaneousRequestWithBufferLimits) {
  config_helper_.setBufferLimits(1024, 1024); // Set buffer limits upstream and downstream.
  simultaneousRequest(1024 * 20, 1024 * 14 + 2, 1024 * 10 + 5, 1024 * 16);
}

TEST_P(Http2UpstreamIntegrationTest, SimultaneousRequestAlpn) {
  use_alpn_ = true;
  simultaneousRequest(1024, 512, 1023, 513);
}

TEST_P(Http2UpstreamIntegrationTest, LargeSimultaneousRequestWithBufferLimitsAlpn) {
  use_alpn_ = true;
  config_helper_.setBufferLimits(1024, 1024); // Set buffer limits upstream and downstream.
  simultaneousRequest(1024 * 20, 1024 * 14 + 2, 1024 * 10 + 5, 1024 * 16);
}

void Http2UpstreamIntegrationTest::manySimultaneousRequests(uint32_t request_bytes, uint32_t) {
  TestRandomGenerator rand;
  const uint32_t num_requests = 50;
  std::vector<Http::RequestEncoder*> encoders;
  std::vector<IntegrationStreamDecoderPtr> responses;
  std::vector<int> response_bytes;
  autonomous_upstream_ = true;
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  for (uint32_t i = 0; i < num_requests; ++i) {
    response_bytes.push_back(rand.random() % (1024 * 2));
    auto headers = Http::TestRequestHeaderMapImpl{
        {":method", "POST"},
        {":path", "/test/long/url"},
        {":scheme", "http"},
        {":authority", "host"},
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

TEST_P(Http2UpstreamIntegrationTest, ManySimultaneousRequest) {
  manySimultaneousRequests(1024, 1024);
}

TEST_P(Http2UpstreamIntegrationTest, ManyLargeSimultaneousRequestWithBufferLimits) {
  EXCLUDE_UPSTREAM_HTTP3; // quic_stream_sequencer.cc:235 CHECK failed: !blocked_.
  config_helper_.setBufferLimits(1024, 1024); // Set buffer limits upstream and downstream.
  manySimultaneousRequests(1024 * 20, 1024 * 20);
}

TEST_P(Http2UpstreamIntegrationTest, ManyLargeSimultaneousRequestWithRandomBackup) {
  EXCLUDE_UPSTREAM_HTTP3; // fails: no 200s.
  config_helper_.addFilter(
      fmt::format(R"EOF(
  name: pause-filter{}
  typed_config:
    "@type": type.googleapis.com/google.protobuf.Empty)EOF",
                  downstreamProtocol() == Http::CodecClient::Type::HTTP3 ? "-for-quic" : ""));

  manySimultaneousRequests(1024 * 20, 1024 * 20);
}

TEST_P(Http2UpstreamIntegrationTest, UpstreamConnectionCloseWithManyStreams) {
  EXCLUDE_UPSTREAM_HTTP3;                     // Close loop.
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
                                                                   {":authority", "host"}});
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
  }
  // Close the connection.
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  // Ensure the streams are all reset successfully.
  for (uint32_t i = 1; i < num_requests; ++i) {
    ASSERT_TRUE(responses[i]->waitForReset());
  }
}

// Regression test for https://github.com/envoyproxy/envoy/issues/6744
TEST_P(Http2UpstreamIntegrationTest, HittingEncoderFilterLimitForGrpc) {
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
  config_helper_.addFilter("{ name: encoder-decoder-buffer-filter, typed_config: { \"@type\": "
                           "type.googleapis.com/google.protobuf.Empty } }");
  config_helper_.setBufferLimits(1024, 1024);
  initialize();

  // Send the request.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"},
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
}

// Tests the default limit for the number of response headers is 100. Results in a stream reset if
// exceeds.
TEST_P(Http2UpstreamIntegrationTest, TestManyResponseHeadersRejected) {
  EXCLUDE_UPSTREAM_HTTP3; // no 503.

  // Default limit for response headers is 100.
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestResponseHeaderMapImpl many_headers(default_response_headers_);
  for (int i = 0; i < 100; i++) {
    many_headers.addCopy("many", std::string(1, 'a'));
  }
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(many_headers, true);
  ASSERT_TRUE(response->waitForEndStream());
  // Upstream stream reset triggered.
  EXPECT_EQ("503", response->headers().getStatusValue());
}

// Tests bootstrap configuration of max response headers.
TEST_P(Http2UpstreamIntegrationTest, ManyResponseHeadersAccepted) {
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
TEST_P(Http2UpstreamIntegrationTest, LargeResponseHeadersRejected) {
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

// Regression test to make sure that configuring upstream logs over gRPC will not crash Envoy.
// TODO(asraa): Test output of the upstream logs.
// See https://github.com/envoyproxy/envoy/issues/8828.
TEST_P(Http2UpstreamIntegrationTest, ConfigureHttpOverGrpcLogs) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        const std::string access_log_name =
            TestEnvironment::temporaryPath(TestUtility::uniqueFilename());
        // Configure just enough of an upstream access log to reference the upstream headers.
        const std::string yaml_string = R"EOF(
name: router
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
  upstream_log:
    name: grpc_accesslog
    filter:
      not_health_check_filter: {}
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.grpc.v3.HttpGrpcAccessLogConfig
      common_config:
        log_name: foo
        transport_api_version: V3
        grpc_service:
          envoy_grpc:
            cluster_name: cluster_0
  )EOF";
        // Replace the terminal envoy.router.
        hcm.clear_http_filters();
        TestUtility::loadFromYaml(yaml_string, *hcm.add_http_filters());
      });

  initialize();

  // Send the request.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Send the response headers.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Regression test for https://github.com/envoyproxy/envoy/issues/13933
TEST_P(Http2UpstreamIntegrationTest, MultipleRequestsLowStreamLimit) {
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
                                     {":authority", "host"},
                                     {AutonomousStream::NO_END_STREAM, ""}});
  // Wait until the response is sent to ensure the SETTINGS frame has been read
  // by Envoy.
  response->waitForHeaders();

  // Now send a second request and make sure it is processed. Previously it
  // would be queued on the original connection, as Envoy would ignore the
  // peer's SETTINGS frame and nghttp2 would then queue it, but now it should
  // result in a second connection and an immediate response.
  FakeStreamPtr upstream_request2;
  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response2->waitForEndStream());
}

// Regression test for https://github.com/envoyproxy/envoy/issues/13933
TEST_P(Http2UpstreamIntegrationTest, UpstreamGoaway) {
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

} // namespace Envoy
