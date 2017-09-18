#include "test/integration/http2_upstream_integration_test.h"

#include <iostream>

#include "common/http/header_map_impl.h"

#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

INSTANTIATE_TEST_CASE_P(IpVersions, Http2UpstreamIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(Http2UpstreamIntegrationTest, RouterNotFound) { testRouterNotFound(); }

TEST_P(Http2UpstreamIntegrationTest, RouterRedirect) { testRouterRedirect(); }

TEST_P(Http2UpstreamIntegrationTest, DrainClose) { testDrainClose(); }

TEST_P(Http2UpstreamIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(lookupPort("http")), 1024, 512, false);
}

TEST_P(Http2UpstreamIntegrationTest, RouterRequestAndResponseWithBodyBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(lookupPort("http_buffer")), 1024, 512,
                                       false);
}

TEST_P(Http2UpstreamIntegrationTest, RouterRequestAndResponseWithZeroByteBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(lookupPort("http")), 0, 0, false);
}

TEST_P(Http2UpstreamIntegrationTest, RouterRequestAndResponseWithZeroByteBodyBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(lookupPort("http_buffer")), 0, 0,
                                       false);
}

TEST_P(Http2UpstreamIntegrationTest, RouterRequestAndResponseWithBodyHttp1) {
  client_protocol_ = Http::CodecClient::Type::HTTP1;
  testRouterRequestAndResponseWithBody(makeClientConnection(lookupPort("http1_buffer")), 1024, 512,
                                       false);
}

TEST_P(Http2UpstreamIntegrationTest, RouterHeaderOnlyRequestAndResponseNoBuffer) {
  testRouterHeaderOnlyRequestAndResponse(makeClientConnection(lookupPort("http")), true);
}

TEST_P(Http2UpstreamIntegrationTest, RouterHeaderOnlyRequestAndResponseBuffer) {
  testRouterHeaderOnlyRequestAndResponse(makeClientConnection(lookupPort("http_buffer")), true);
}

TEST_P(Http2UpstreamIntegrationTest, RouterHeaderOnlyRequestAndResponseHttp1) {
  client_protocol_ = Http::CodecClient::Type::HTTP1;
  testRouterHeaderOnlyRequestAndResponse(makeClientConnection(lookupPort("http1_buffer")), true);
}

TEST_P(Http2UpstreamIntegrationTest, RouterUpstreamDisconnectBeforeRequestcomplete) {
  testRouterUpstreamDisconnectBeforeRequestComplete(makeClientConnection(lookupPort("http")));
}

TEST_P(Http2UpstreamIntegrationTest, RouterUpstreamDisconnectBeforeResponseComplete) {
  testRouterUpstreamDisconnectBeforeResponseComplete(makeClientConnection(lookupPort("http")));
}

TEST_P(Http2UpstreamIntegrationTest, RouterDownstreamDisconnectBeforeRequestComplete) {
  testRouterDownstreamDisconnectBeforeRequestComplete(makeClientConnection(lookupPort("http")));
}

TEST_P(Http2UpstreamIntegrationTest, RouterDownstreamDisconnectBeforeResponseComplete) {
  testRouterDownstreamDisconnectBeforeResponseComplete(makeClientConnection(lookupPort("http")));
}

TEST_P(Http2UpstreamIntegrationTest, RouterUpstreamResponseBeforeRequestComplete) {
  testRouterUpstreamResponseBeforeRequestComplete(makeClientConnection(lookupPort("http")));
}

TEST_P(Http2UpstreamIntegrationTest, TwoRequests) { testTwoRequests(); }

TEST_P(Http2UpstreamIntegrationTest, Retry) { testRetry(); }

TEST_P(Http2UpstreamIntegrationTest, RetryHittingBufferLimit) { testRetryHittingBufferLimit(); }

TEST_P(Http2UpstreamIntegrationTest, GrpcRetry) { testGrpcRetry(); }

TEST_P(Http2UpstreamIntegrationTest, DownstreamResetBeforeResponseComplete) {
  testDownstreamResetBeforeResponseComplete();
}

TEST_P(Http2UpstreamIntegrationTest, Trailers) { testTrailers(1024, 2048); }

void Http2UpstreamIntegrationTest::bidirectionalStreaming(uint32_t port, uint32_t bytes) {
  executeActions(
      {[&]() -> void { codec_client_ = makeHttpConnection(port); },
       // Start request
       [&]() -> void {
         request_encoder_ =
             &codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                  {":path", "/test/long/url"},
                                                                  {":scheme", "http"},
                                                                  {":authority", "host"}},
                                          *response_);
       },
       [&]() -> void {
         fake_upstream_connection_ = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
       },
       [&]() -> void { upstream_request_ = fake_upstream_connection_->waitForNewStream(); },

       // Send some data
       [&]() -> void {
         codec_client_->sendData(*request_encoder_, bytes, false);

       },
       [&]() -> void { upstream_request_->waitForData(*dispatcher_, bytes); },

       // Start response
       [&]() -> void {
         upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
         upstream_request_->encodeData(bytes, false);
       },
       [&]() -> void { response_->waitForBodyData(bytes); },

       // Finish request
       [&]() -> void {
         codec_client_->sendTrailers(*request_encoder_,
                                     Http::TestHeaderMapImpl{{"trailer", "foo"}});

       },
       [&]() -> void { upstream_request_->waitForEndStream(*dispatcher_); },

       // Finish response
       [&]() -> void {
         upstream_request_->encodeTrailers(Http::TestHeaderMapImpl{{"trailer", "bar"}});
       },
       [&]() -> void { response_->waitForEndStream(); },

       // Cleanup both downstream and upstream
       [&]() -> void { codec_client_->close(); },
       [&]() -> void { fake_upstream_connection_->close(); },
       [&]() -> void { fake_upstream_connection_->waitForDisconnect(); }});

  EXPECT_TRUE(response_->complete());
}

TEST_P(Http2UpstreamIntegrationTest, BidirectionalStreaming) {
  bidirectionalStreaming(lookupPort("http"), 1024);
}

TEST_P(Http2UpstreamIntegrationTest, LargeBidirectionalStreamingWithBufferLimits) {
  bidirectionalStreaming(lookupPort("http_with_buffer_limits"), 1024 * 32);
}

TEST_P(Http2UpstreamIntegrationTest, BidirectionalStreamingReset) {
  executeActions(
      {[&]() -> void { codec_client_ = makeHttpConnection(lookupPort("http")); },
       // Start request
       [&]() -> void {
         request_encoder_ =
             &codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                  {":path", "/test/long/url"},
                                                                  {":scheme", "http"},
                                                                  {":authority", "host"}},
                                          *response_);
       },
       [&]() -> void {
         fake_upstream_connection_ = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
       },
       [&]() -> void { upstream_request_ = fake_upstream_connection_->waitForNewStream(); },

       // Send some data
       [&]() -> void {
         codec_client_->sendData(*request_encoder_, 1024, false);

       },
       [&]() -> void { upstream_request_->waitForData(*dispatcher_, 1024); },

       // Start response
       [&]() -> void {
         upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
         upstream_request_->encodeData(1024, false);
       },
       [&]() -> void { response_->waitForBodyData(1024); },

       // Finish request
       [&]() -> void {
         codec_client_->sendTrailers(*request_encoder_,
                                     Http::TestHeaderMapImpl{{"trailer", "foo"}});

       },
       [&]() -> void { upstream_request_->waitForEndStream(*dispatcher_); },

       // Reset
       [&]() -> void { upstream_request_->encodeResetStream(); },
       [&]() -> void { response_->waitForReset(); },

       // Cleanup both downstream and upstream
       [&]() -> void { codec_client_->close(); },
       [&]() -> void { fake_upstream_connection_->close(); },
       [&]() -> void { fake_upstream_connection_->waitForDisconnect(); }});

  EXPECT_FALSE(response_->complete());
}

void Http2UpstreamIntegrationTest::simultaneousRequest(uint32_t port, uint32_t request1_bytes,
                                                       uint32_t request2_bytes,
                                                       uint32_t response1_bytes,
                                                       uint32_t response2_bytes) {
  Http::StreamEncoder* encoder1;
  Http::StreamEncoder* encoder2;
  IntegrationStreamDecoderPtr response1(new IntegrationStreamDecoder(*dispatcher_));
  IntegrationStreamDecoderPtr response2(new IntegrationStreamDecoder(*dispatcher_));
  FakeStreamPtr upstream_request1;
  FakeStreamPtr upstream_request2;
  executeActions(
      {[&]() -> void { codec_client_ = makeHttpConnection(port); },
       // Start request 1
       [&]() -> void {
         encoder1 =
             &codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                  {":path", "/test/long/url"},
                                                                  {":scheme", "http"},
                                                                  {":authority", "host"}},
                                          *response1);
       },
       [&]() -> void {
         fake_upstream_connection_ = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
       },
       [&]() -> void { upstream_request1 = fake_upstream_connection_->waitForNewStream(); },

       // Start request 2
       [&]() -> void {
         encoder2 =
             &codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                  {":path", "/test/long/url"},
                                                                  {":scheme", "http"},
                                                                  {":authority", "host"}},
                                          *response2);
       },
       [&]() -> void { upstream_request2 = fake_upstream_connection_->waitForNewStream(); },

       // Finish request 1
       [&]() -> void {
         codec_client_->sendData(*encoder1, request1_bytes, true);

       },
       [&]() -> void { upstream_request1->waitForEndStream(*dispatcher_); },

       // Finish request 2
       [&]() -> void {
         codec_client_->sendData(*encoder2, request2_bytes, true);

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
       [&]() -> void { codec_client_->close(); },
       [&]() -> void { fake_upstream_connection_->close(); },
       [&]() -> void { fake_upstream_connection_->waitForDisconnect(); }});
}

TEST_P(Http2UpstreamIntegrationTest, SimultaneousRequest) {
  simultaneousRequest(lookupPort("http"), 1024, 512, 1023, 513);
}

TEST_P(Http2UpstreamIntegrationTest, LargeSimultaneousRequestWithBufferLimits) {
  simultaneousRequest(lookupPort("http_with_buffer_limits"), 1024 * 20, 1024 * 14 + 2,
                      1024 * 10 + 5, 1024 * 16);
}

void Http2UpstreamIntegrationTest::manySimultaneousRequests(uint32_t port, uint32_t request_bytes,
                                                            uint32_t) {
  TestRandomGenerator rand;
  const uint32_t num_requests = 50;
  std::vector<Http::StreamEncoder*> encoders;
  std::vector<IntegrationStreamDecoderPtr> responses;
  std::vector<FakeStreamPtr> upstream_requests;
  executeActions(
      {[&]() -> void { codec_client_ = makeHttpConnection(port); },
       [&]() -> void {
         for (uint32_t i = 0; i < num_requests; ++i) {
           responses.push_back(
               IntegrationStreamDecoderPtr{new IntegrationStreamDecoder(*dispatcher_)});
           encoders.push_back(
               &codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                    {":path", "/test/long/url"},
                                                                    {":scheme", "http"},
                                                                    {":authority", "host"}},
                                            *responses[i]));
           codec_client_->sendData(*encoders[i], request_bytes, true);
         }
       },
       [&]() -> void {
         fake_upstream_connection_ = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
         for (uint32_t i = 0; i < num_requests; ++i) {
           // As data and streams are interwoven, make sure waitForNewStream()
           // ignores incoming data and waits for actual stream establishment.
           upstream_requests.push_back(fake_upstream_connection_->waitForNewStream(true));
         }
       },
       [&]() -> void {
         for (uint32_t i = 0; i < num_requests; ++i) {
           upstream_requests[i]->waitForEndStream(*dispatcher_);
           if (i % 2 == 0) {
             upstream_requests[i]->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}},
                                                 false);
             upstream_requests[i]->encodeData(rand.random() % (1024 * 2), true);
           } else {
             upstream_requests[i]->encodeResetStream();
           }
         }
       },
       [&]() -> void {
         for (uint32_t i = 0; i < num_requests; ++i) {
           responses[i]->waitForEndStream();
           if (i % 2 == 0) {
             EXPECT_TRUE(upstream_requests[i]->complete());
             EXPECT_EQ(request_bytes, upstream_requests[i]->bodyLength());

             EXPECT_TRUE(responses[i]->complete());
             EXPECT_STREQ("200", responses[i]->headers().Status()->value().c_str());
           }
         }
       },

       // Cleanup both downstream and upstream
       [&]() -> void { codec_client_->close(); },
       [&]() -> void { fake_upstream_connection_->close(); },
       [&]() -> void { fake_upstream_connection_->waitForDisconnect(); }});
}

TEST_P(Http2UpstreamIntegrationTest, ManySimultaneousRequest) {
  manySimultaneousRequests(lookupPort("http"), 1024, 1024);
}

TEST_P(Http2UpstreamIntegrationTest, ManyLargeSimultaneousRequestWithBufferLimits) {
  manySimultaneousRequests(lookupPort("http_with_buffer_limits"), 1024 * 20, 1024 * 20);
}

TEST_P(Http2UpstreamIntegrationTest, UpstreamConnectionCloseWithManyStreams) {
  uint32_t port = lookupPort("http_with_buffer_limits");
  TestRandomGenerator rand;
  const uint32_t num_requests = rand.random() % 50 + 1;
  std::vector<Http::StreamEncoder*> encoders;
  std::vector<IntegrationStreamDecoderPtr> responses;
  std::vector<FakeStreamPtr> upstream_requests;
  executeActions(
      {[&]() -> void { codec_client_ = makeHttpConnection(port); },
       [&]() -> void {
         for (uint32_t i = 0; i < num_requests; ++i) {
           responses.push_back(
               IntegrationStreamDecoderPtr{new IntegrationStreamDecoder(*dispatcher_)});
           encoders.push_back(
               &codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                    {":path", "/test/long/url"},
                                                                    {":scheme", "http"},
                                                                    {":authority", "host"}},
                                            *responses[i]));
           // Reset a few streams to test how reset and watermark interact.
           if (i % 15 == 0) {
             codec_client_->sendReset(*encoders[i]);
           } else {
             codec_client_->sendData(*encoders[i], 0, true);
           }
         }
       },
       [&]() -> void {
         fake_upstream_connection_ = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
         for (uint32_t i = 0; i < num_requests; ++i) {
           upstream_requests.push_back(fake_upstream_connection_->waitForNewStream());
         }
         for (uint32_t i = 0; i < num_requests; ++i) {
           if (i % 15 != 0) {
             upstream_requests[i]->waitForEndStream(*dispatcher_);
             upstream_requests[i]->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}},
                                                 false);
             upstream_requests[i]->encodeData(100, false);
           }
         }
         // Close the connection.
         fake_upstream_connection_->close();
       },
       [&]() -> void {
         // Ensure the streams are all reset successfully.
         for (uint32_t i = 0; i < num_requests; ++i) {
           if (i % 15 != 0) {
             responses[i]->waitForReset();
           }
         }
       },

       // Cleanup both downstream and upstream
       [&]() -> void { codec_client_->close(); },
       [&]() -> void { fake_upstream_connection_->waitForDisconnect(); }});
}

} // namespace Envoy
