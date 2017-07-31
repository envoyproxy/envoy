#include "test/integration/http2_upstream_integration_test.h"

#include "common/http/header_map_impl.h"

#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

INSTANTIATE_TEST_CASE_P(IpVersions, Http2UpstreamIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(Http2UpstreamIntegrationTest, RouterNotFound) {
  testRouterNotFound(Http::CodecClient::Type::HTTP2);
}

TEST_P(Http2UpstreamIntegrationTest, RouterRedirect) {
  testRouterRedirect(Http::CodecClient::Type::HTTP2);
}

TEST_P(Http2UpstreamIntegrationTest, DrainClose) { testDrainClose(Http::CodecClient::Type::HTTP2); }

TEST_P(Http2UpstreamIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(lookupPort("http")),
                                       Http::CodecClient::Type::HTTP2, 1024, 512, false);
}

TEST_P(Http2UpstreamIntegrationTest, RouterRequestAndResponseWithBodyBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(lookupPort("http_buffer")),
                                       Http::CodecClient::Type::HTTP2, 1024, 512, false);
}

TEST_P(Http2UpstreamIntegrationTest, RouterRequestAndResponseWithZeroByteBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(lookupPort("http")),
                                       Http::CodecClient::Type::HTTP2, 0, 0, false);
}

TEST_P(Http2UpstreamIntegrationTest, RouterRequestAndResponseWithZeroByteBodyBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(lookupPort("http_buffer")),
                                       Http::CodecClient::Type::HTTP2, 0, 0, false);
}

TEST_P(Http2UpstreamIntegrationTest, RouterRequestAndResponseWithBodyHttp1) {
  testRouterRequestAndResponseWithBody(makeClientConnection(lookupPort("http1_buffer")),
                                       Http::CodecClient::Type::HTTP1, 1024, 512, false);
}

TEST_P(Http2UpstreamIntegrationTest, RouterHeaderOnlyRequestAndResponseNoBuffer) {
  testRouterHeaderOnlyRequestAndResponse(makeClientConnection(lookupPort("http")),
                                         Http::CodecClient::Type::HTTP2, true);
}

TEST_P(Http2UpstreamIntegrationTest, RouterHeaderOnlyRequestAndResponseBuffer) {
  testRouterHeaderOnlyRequestAndResponse(makeClientConnection(lookupPort("http_buffer")),
                                         Http::CodecClient::Type::HTTP2, true);
}

TEST_P(Http2UpstreamIntegrationTest, RouterHeaderOnlyRequestAndResponseHttp1) {
  testRouterHeaderOnlyRequestAndResponse(makeClientConnection(lookupPort("http1_buffer")),
                                         Http::CodecClient::Type::HTTP1, true);
}

TEST_P(Http2UpstreamIntegrationTest, RouterUpstreamDisconnectBeforeRequestcomplete) {
  testRouterUpstreamDisconnectBeforeRequestComplete(makeClientConnection(lookupPort("http")),
                                                    Http::CodecClient::Type::HTTP2);
}

TEST_P(Http2UpstreamIntegrationTest, RouterUpstreamDisconnectBeforeResponseComplete) {
  testRouterUpstreamDisconnectBeforeResponseComplete(makeClientConnection(lookupPort("http")),
                                                     Http::CodecClient::Type::HTTP2);
}

TEST_P(Http2UpstreamIntegrationTest, RouterDownstreamDisconnectBeforeRequestComplete) {
  testRouterDownstreamDisconnectBeforeRequestComplete(makeClientConnection(lookupPort("http")),
                                                      Http::CodecClient::Type::HTTP2);
}

TEST_P(Http2UpstreamIntegrationTest, RouterDownstreamDisconnectBeforeResponseComplete) {
  testRouterDownstreamDisconnectBeforeResponseComplete(makeClientConnection(lookupPort("http")),
                                                       Http::CodecClient::Type::HTTP2);
}

TEST_P(Http2UpstreamIntegrationTest, RouterUpstreamResponseBeforeRequestComplete) {
  testRouterUpstreamResponseBeforeRequestComplete(makeClientConnection(lookupPort("http")),
                                                  Http::CodecClient::Type::HTTP2);
}

TEST_P(Http2UpstreamIntegrationTest, TwoRequests) {
  testTwoRequests(Http::CodecClient::Type::HTTP2);
}

TEST_P(Http2UpstreamIntegrationTest, Retry) { testRetry(Http::CodecClient::Type::HTTP2); }

TEST_P(Http2UpstreamIntegrationTest, GrpcRetry) { testGrpcRetry(); }

TEST_P(Http2UpstreamIntegrationTest, DownstreamResetBeforeResponseComplete) {
  testDownstreamResetBeforeResponseComplete();
}

TEST_P(Http2UpstreamIntegrationTest, Trailers) { testTrailers(1024, 2048); }

void Http2UpstreamIntegrationTest::bidirectionalStreaming(uint32_t port, uint32_t bytes) {
  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  Http::StreamEncoder* encoder;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  FakeStreamPtr upstream_request;
  executeActions(
      {[&]() -> void { codec_client = makeHttpConnection(port, Http::CodecClient::Type::HTTP2); },
       // Start request
       [&]() -> void {
         encoder = &codec_client->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                       {":path", "/test/long/url"},
                                                                       {":scheme", "http"},
                                                                       {":authority", "host"}},
                                               *response);
       },
       [&]() -> void {
         fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
       },
       [&]() -> void { upstream_request = fake_upstream_connection->waitForNewStream(); },

       // Send some data
       [&]() -> void {
         codec_client->sendData(*encoder, bytes, false);

       },
       [&]() -> void { upstream_request->waitForData(*dispatcher_, bytes); },

       // Start response
       [&]() -> void {
         upstream_request->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
         upstream_request->encodeData(bytes, false);
       },
       [&]() -> void { response->waitForBodyData(bytes); },

       // Finish request
       [&]() -> void {
         codec_client->sendTrailers(*encoder, Http::TestHeaderMapImpl{{"trailer", "foo"}});

       },
       [&]() -> void { upstream_request->waitForEndStream(*dispatcher_); },

       // Finish response
       [&]() -> void {
         upstream_request->encodeTrailers(Http::TestHeaderMapImpl{{"trailer", "bar"}});
       },
       [&]() -> void { response->waitForEndStream(); },

       // Cleanup both downstream and upstream
       [&]() -> void { codec_client->close(); },
       [&]() -> void { fake_upstream_connection->close(); },
       [&]() -> void { fake_upstream_connection->waitForDisconnect(); }});

  EXPECT_TRUE(response->complete());
}

TEST_P(Http2UpstreamIntegrationTest, BidirectionalStreaming) {
  bidirectionalStreaming(lookupPort("http"), 1024);
}

TEST_P(Http2UpstreamIntegrationTest, LargeBidirectionalStreamingWithBufferLimits) {
  bidirectionalStreaming(lookupPort("http_with_buffer_limits"), 1024 * 32);
}

TEST_P(Http2UpstreamIntegrationTest, BidirectionalStreamingReset) {
  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  Http::StreamEncoder* encoder;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  FakeStreamPtr upstream_request;
  executeActions(
      {[&]() -> void {
         codec_client = makeHttpConnection(lookupPort("http"), Http::CodecClient::Type::HTTP2);
       },
       // Start request
       [&]() -> void {
         encoder = &codec_client->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                       {":path", "/test/long/url"},
                                                                       {":scheme", "http"},
                                                                       {":authority", "host"}},
                                               *response);
       },
       [&]() -> void {
         fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
       },
       [&]() -> void { upstream_request = fake_upstream_connection->waitForNewStream(); },

       // Send some data
       [&]() -> void {
         codec_client->sendData(*encoder, 1024, false);

       },
       [&]() -> void { upstream_request->waitForData(*dispatcher_, 1024); },

       // Start response
       [&]() -> void {
         upstream_request->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
         upstream_request->encodeData(1024, false);
       },
       [&]() -> void { response->waitForBodyData(1024); },

       // Finish request
       [&]() -> void {
         codec_client->sendTrailers(*encoder, Http::TestHeaderMapImpl{{"trailer", "foo"}});

       },
       [&]() -> void { upstream_request->waitForEndStream(*dispatcher_); },

       // Reset
       [&]() -> void { upstream_request->encodeResetStream(); },
       [&]() -> void { response->waitForReset(); },

       // Cleanup both downstream and upstream
       [&]() -> void { codec_client->close(); },
       [&]() -> void { fake_upstream_connection->close(); },
       [&]() -> void { fake_upstream_connection->waitForDisconnect(); }});

  EXPECT_FALSE(response->complete());
}

void Http2UpstreamIntegrationTest::simultaneousRequest(uint32_t port, uint32_t request1_bytes,
                                                       uint32_t request2_bytes,
                                                       uint32_t response1_bytes,
                                                       uint32_t response2_bytes) {
  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  Http::StreamEncoder* encoder1;
  Http::StreamEncoder* encoder2;
  IntegrationStreamDecoderPtr response1(new IntegrationStreamDecoder(*dispatcher_));
  IntegrationStreamDecoderPtr response2(new IntegrationStreamDecoder(*dispatcher_));
  FakeStreamPtr upstream_request1;
  FakeStreamPtr upstream_request2;
  executeActions(
      {[&]() -> void { codec_client = makeHttpConnection(port, Http::CodecClient::Type::HTTP2); },
       // Start request 1
       [&]() -> void {
         encoder1 = &codec_client->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                        {":path", "/test/long/url"},
                                                                        {":scheme", "http"},
                                                                        {":authority", "host"}},
                                                *response1);
       },
       [&]() -> void {
         fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
       },
       [&]() -> void { upstream_request1 = fake_upstream_connection->waitForNewStream(); },

       // Start request 2
       [&]() -> void {
         response2.reset(new IntegrationStreamDecoder(*dispatcher_));
         encoder2 = &codec_client->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                        {":path", "/test/long/url"},
                                                                        {":scheme", "http"},
                                                                        {":authority", "host"}},
                                                *response2);
       },
       [&]() -> void { upstream_request2 = fake_upstream_connection->waitForNewStream(); },

       // Finish request 1
       [&]() -> void {
         codec_client->sendData(*encoder1, request1_bytes, true);

       },
       [&]() -> void { upstream_request1->waitForEndStream(*dispatcher_); },

       // Finish request 2
       [&]() -> void {
         codec_client->sendData(*encoder2, request2_bytes, true);

       },
       [&]() -> void { upstream_request2->waitForEndStream(*dispatcher_); },

       // Respond request 2
       [&]() -> void {
         upstream_request2->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
         upstream_request2->encodeData(response2_bytes, true);
       },
       [&]() -> void {
         response2->waitForEndStream();
         EXPECT_TRUE(upstream_request2->complete());
         EXPECT_EQ(request2_bytes, upstream_request2->bodyLength());

         EXPECT_TRUE(response2->complete());
         EXPECT_STREQ("200", response2->headers().Status()->value().c_str());
         EXPECT_EQ(response2_bytes, response2->body().size());
       },

       // Respond request 1
       [&]() -> void {
         upstream_request1->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
         upstream_request1->encodeData(response1_bytes, true);
       },
       [&]() -> void {
         response1->waitForEndStream();
         EXPECT_TRUE(upstream_request1->complete());
         EXPECT_EQ(request1_bytes, upstream_request1->bodyLength());

         EXPECT_TRUE(response1->complete());
         EXPECT_STREQ("200", response1->headers().Status()->value().c_str());
         EXPECT_EQ(response1_bytes, response1->body().size());
       },

       // Cleanup both downstream and upstream
       [&]() -> void { codec_client->close(); },
       [&]() -> void { fake_upstream_connection->close(); },
       [&]() -> void { fake_upstream_connection->waitForDisconnect(); }});
}

TEST_P(Http2UpstreamIntegrationTest, SimultaneousRequest) {
  simultaneousRequest(lookupPort("http"), 1024, 512, 1023, 513);
}

TEST_P(Http2UpstreamIntegrationTest, LargeSimultaneousRequestWithBufferLimits) {
  simultaneousRequest(lookupPort("http_with_buffer_limits"), 1024 * 20, 1024 * 14 + 2,
                      1024 * 10 + 5, 1024 * 16);
}

} // namespace Envoy
