#include "test/integration/http2_upstream_integration_test.h"

#include <iostream>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "common/http/header_map_impl.h"

#include "test/integration/autonomous_upstream.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

INSTANTIATE_TEST_SUITE_P(IpVersions, Http2UpstreamIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

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
  testRouterUpstreamDisconnectBeforeRequestComplete();
}

TEST_P(Http2UpstreamIntegrationTest, RouterUpstreamDisconnectBeforeResponseComplete) {
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

TEST_P(Http2UpstreamIntegrationTest, Retry) { testRetry(); }

TEST_P(Http2UpstreamIntegrationTest, GrpcRetry) { testGrpcRetry(); }

TEST_P(Http2UpstreamIntegrationTest, Trailers) { testTrailers(1024, 2048, true, true); }

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
  upstream_request_->encodeTrailers(Http::TestHeaderMapImpl{{"trailer", "bar"}});
  response->waitForEndStream();
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
  response->waitForReset();
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
  response2->waitForEndStream();
  EXPECT_TRUE(upstream_request2->complete());
  EXPECT_EQ(request2_bytes, upstream_request2->bodyLength());
  EXPECT_TRUE(response2->complete());
  EXPECT_EQ("200", response2->headers().Status()->value().getStringView());
  EXPECT_EQ(response2_bytes, response2->body().size());

  // Respond to request 1
  upstream_request1->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request1->encodeData(response1_bytes, true);
  response1->waitForEndStream();
  EXPECT_TRUE(upstream_request1->complete());
  EXPECT_EQ(request1_bytes, upstream_request1->bodyLength());
  EXPECT_TRUE(response1->complete());
  EXPECT_EQ("200", response1->headers().Status()->value().getStringView());
  EXPECT_EQ(response1_bytes, response1->body().size());
}

TEST_P(Http2UpstreamIntegrationTest, SimultaneousRequest) {
  simultaneousRequest(1024, 512, 1023, 513);
}

TEST_P(Http2UpstreamIntegrationTest, LargeSimultaneousRequestWithBufferLimits) {
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
    responses[i]->waitForEndStream();
    if (i % 2 != 0) {
      EXPECT_TRUE(responses[i]->complete());
      EXPECT_EQ("200", responses[i]->headers().Status()->value().getStringView());
      EXPECT_EQ(response_bytes[i], responses[i]->body().length());
    } else {
      // Upstream stream reset.
      EXPECT_EQ("503", responses[i]->headers().Status()->value().getStringView());
    }
  }
}

TEST_P(Http2UpstreamIntegrationTest, ManySimultaneousRequest) {
  manySimultaneousRequests(1024, 1024);
}

TEST_P(Http2UpstreamIntegrationTest, ManyLargeSimultaneousRequestWithBufferLimits) {
  config_helper_.setBufferLimits(1024, 1024); // Set buffer limits upstream and downstream.
  manySimultaneousRequests(1024 * 20, 1024 * 20);
}

TEST_P(Http2UpstreamIntegrationTest, ManyLargeSimultaneousRequestWithRandomBackup) {
  config_helper_.addFilter(R"EOF(
  name: random-pause-filter
  typed_config:
    "@type": type.googleapis.com/google.protobuf.Empty
  )EOF");

  manySimultaneousRequests(1024 * 20, 1024 * 20);
}

TEST_P(Http2UpstreamIntegrationTest, UpstreamConnectionCloseWithManyStreams) {
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
    responses[i]->waitForReset();
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
        const std::string yaml_string = R"EOF(
name: router
typed_config:
  "@type": type.googleapis.com/envoy.config.filter.http.router.v2.Router
  upstream_log:
    name: accesslog
    filter:
      not_health_check_filter: {}
    typed_config:
      "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
      path: /dev/null
  )EOF";
        TestUtility::loadFromYaml(yaml_string, *hcm.mutable_http_filters(1));
      });

  // As with ProtocolIntegrationTest.HittingEncoderFilterLimit use a filter
  // which buffers response data but in this case, make sure the sendLocalReply
  // is gRPC.
  config_helper_.addFilter("{ name: envoy.filters.http.dynamo, typed_config: { \"@type\": "
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
  Buffer::OwnedImpl data(R"({"TableName":"locations"})");
  codec_client_->sendData(*downstream_request, data, true);
  waitForNextUpstreamRequest();

  // Send the response headers.
  upstream_request_->encodeHeaders(default_response_headers_, false);

  // Now send an overly large response body. At some point, too much data will
  // be buffered, the stream will be reset, and the connection will disconnect.
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  upstream_request_->encodeData(1024 * 65, false);
  ASSERT_TRUE(upstream_request_->waitForReset());
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  response->waitForEndStream();
  EXPECT_TRUE(response->complete());
}

// Tests the default limit for the number of response headers is 100. Results in a stream reset if
// exceeds.
TEST_P(Http2UpstreamIntegrationTest, TestManyResponseHeadersRejected) {
  // Default limit for response headers is 100.
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestHeaderMapImpl many_headers(default_response_headers_);
  for (int i = 0; i < 100; i++) {
    many_headers.addCopy("many", std::string(1, 'a'));
  }
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(many_headers, true);
  response->waitForEndStream();
  // Upstream stream reset triggered.
  EXPECT_EQ("503", response->headers().Status()->value().getStringView());
}

// Tests bootstrap configuration of max response headers.
TEST_P(Http2UpstreamIntegrationTest, ManyResponseHeadersAccepted) {
  // Set max response header count to 200.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    auto* cluster = static_resources->mutable_clusters(0);
    auto* http_protocol_options = cluster->mutable_common_http_protocol_options();
    http_protocol_options->mutable_max_headers_count()->set_value(200);
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
  response->waitForEndStream();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
}

// Tests that HTTP/2 response headers over 60 kB are rejected and result in a stream reset.
TEST_P(Http2UpstreamIntegrationTest, LargeResponseHeadersRejected) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestHeaderMapImpl large_headers(default_response_headers_);
  large_headers.addCopy("large", std::string(60 * 1024, 'a'));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(large_headers, true);
  response->waitForEndStream();
  // Upstream stream reset.
  EXPECT_EQ("503", response->headers().Status()->value().getStringView());
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
  "@type": type.googleapis.com/envoy.config.filter.http.router.v2.Router
  upstream_log:
    name: grpc_accesslog
    filter:
      not_health_check_filter: {}
    typed_config:
      "@type": type.googleapis.com/envoy.config.accesslog.v2.HttpGrpcAccessLogConfig
      common_config:
        log_name: foo
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
  response->waitForEndStream();
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
}

} // namespace Envoy
