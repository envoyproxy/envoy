#include "test/integration/http2_integration_test.h"

#include <string>

#include "common/buffer/buffer_impl.h"
#include "common/http/header_map_impl.h"

#include "test/integration/utility.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

INSTANTIATE_TEST_CASE_P(IpVersions, Http2IntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(Http2IntegrationTest, RouterNotFound) { testRouterNotFound(Http::CodecClient::Type::HTTP2); }

TEST_P(Http2IntegrationTest, RouterNotFoundBodyNoBuffer) {
  testRouterNotFoundWithBody(lookupPort("http"), Http::CodecClient::Type::HTTP2);
}

TEST_P(Http2IntegrationTest, RouterNotFoundBodyBuffer) {
  testRouterNotFoundWithBody(lookupPort("http_buffer"), Http::CodecClient::Type::HTTP2);
}

TEST_P(Http2IntegrationTest, RouterRedirect) { testRouterRedirect(Http::CodecClient::Type::HTTP2); }

TEST_P(Http2IntegrationTest, DrainClose) { testDrainClose(Http::CodecClient::Type::HTTP2); }

TEST_P(Http2IntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(lookupPort("http")),
                                       Http::CodecClient::Type::HTTP2, 1024, 512, false);
}

TEST_P(Http2IntegrationTest, RouterRequestAndResponseWithBodyBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(lookupPort("http_buffer")),
                                       Http::CodecClient::Type::HTTP2, 1024, 512, false);
}

TEST_P(Http2IntegrationTest, RouterRequestAndResponseWithGiantBodyBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(lookupPort("http_buffer")),
                                       Http::CodecClient::Type::HTTP2, 1024 * 1024, 1024 * 1024,
                                       false);
}

TEST_P(Http2IntegrationTest, RouterHeaderOnlyRequestAndResponseNoBuffer) {
  testRouterHeaderOnlyRequestAndResponse(makeClientConnection(lookupPort("http")),
                                         Http::CodecClient::Type::HTTP2);
}

TEST_P(Http2IntegrationTest, RouterHeaderOnlyRequestAndResponseBuffer) {
  testRouterHeaderOnlyRequestAndResponse(makeClientConnection(lookupPort("http_buffer")),
                                         Http::CodecClient::Type::HTTP2);
}

TEST_P(Http2IntegrationTest, RouterRequestAndResponseLargeHeaderNoBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(lookupPort("http")),
                                       Http::CodecClient::Type::HTTP2, 1024, 512, true);
}

TEST_P(Http2IntegrationTest, RouterUpstreamDisconnectBeforeRequestcomplete) {
  testRouterUpstreamDisconnectBeforeRequestComplete(makeClientConnection(lookupPort("http")),
                                                    Http::CodecClient::Type::HTTP2);
}

TEST_P(Http2IntegrationTest, RouterUpstreamDisconnectBeforeResponseComplete) {
  testRouterUpstreamDisconnectBeforeResponseComplete(makeClientConnection(lookupPort("http")),
                                                     Http::CodecClient::Type::HTTP2);
}

TEST_P(Http2IntegrationTest, RouterDownstreamDisconnectBeforeRequestComplete) {
  testRouterDownstreamDisconnectBeforeRequestComplete(makeClientConnection(lookupPort("http")),
                                                      Http::CodecClient::Type::HTTP2);
}

TEST_P(Http2IntegrationTest, RouterDownstreamDisconnectBeforeResponseComplete) {
  testRouterDownstreamDisconnectBeforeResponseComplete(makeClientConnection(lookupPort("http")),
                                                       Http::CodecClient::Type::HTTP2);
}

TEST_P(Http2IntegrationTest, RouterUpstreamResponseBeforeRequestComplete) {
  testRouterUpstreamResponseBeforeRequestComplete(makeClientConnection(lookupPort("http")),
                                                  Http::CodecClient::Type::HTTP2);
}

TEST_P(Http2IntegrationTest, TwoRequests) { testTwoRequests(Http::CodecClient::Type::HTTP2); }

TEST_P(Http2IntegrationTest, Retry) { testRetry(Http::CodecClient::Type::HTTP2); }

TEST_P(Http2IntegrationTest, GrpcRetry) { testGrpcRetry(); }

TEST_P(Http2IntegrationTest, MaxHeadersInCodec) {
  Http::TestHeaderMapImpl big_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};

  big_headers.addViaCopy("big", std::string(63 * 1024, 'a'));

  IntegrationCodecClientPtr codec_client;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  Http::StreamEncoder* downstream_request{};
  executeActions(
      {[&]() -> void {
        codec_client = makeHttpConnection(lookupPort("http"), Http::CodecClient::Type::HTTP2);
      },
       [&]() -> void { downstream_request = &codec_client->startRequest(big_headers, *response); },
       [&]() -> void { response->waitForReset(); }, [&]() -> void { codec_client->close(); }});
}

TEST_P(Http2IntegrationTest, DownstreamResetBeforeResponseComplete) {
  testDownstreamResetBeforeResponseComplete();
}

TEST_P(Http2IntegrationTest, BadMagic) {
  Buffer::OwnedImpl buffer("hello");
  std::string response;
  RawConnectionDriver connection(
      lookupPort("http"),
      buffer, [&](Network::ClientConnection&, const Buffer::Instance& data) -> void {
        response.append(TestUtility::bufferToString(data));
      }, version_);

  connection.run();
  EXPECT_EQ("", response);
}

TEST_P(Http2IntegrationTest, BadFrame) {
  Buffer::OwnedImpl buffer("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\nhelloworldcauseanerror");
  std::string response;
  RawConnectionDriver connection(
      lookupPort("http"),
      buffer, [&](Network::ClientConnection&, const Buffer::Instance& data) -> void {
        response.append(TestUtility::bufferToString(data));
      }, version_);

  connection.run();
  EXPECT_TRUE(response.find("SETTINGS expected") != std::string::npos);
}

TEST_P(Http2IntegrationTest, GoAway) {
  IntegrationCodecClientPtr codec_client;
  Http::StreamEncoder* encoder;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  executeActions(
      {[&]() -> void {
        codec_client = makeHttpConnection(lookupPort("http"), Http::CodecClient::Type::HTTP2);
      },
       [&]() -> void {
         encoder = &codec_client->startRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                       {":path", "/healthcheck"},
                                                                       {":scheme", "http"},
                                                                       {":authority", "host"}},
                                               *response);
       },
       [&]() -> void { codec_client->goAway(); },
       [&]() -> void { codec_client->sendData(*encoder, 0, true); },
       [&]() -> void { response->waitForEndStream(); }, [&]() -> void { codec_client->close(); }});

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}

TEST_P(Http2IntegrationTest, Trailers) { testTrailers(1024, 2048); }

TEST_P(Http2IntegrationTest, TrailersGiantBody) { testTrailers(1024 * 1024, 1024 * 1024); }

TEST_P(Http2IntegrationTest, SimultaneousRequest) {
  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection1;
  FakeHttpConnectionPtr fake_upstream_connection2;
  Http::StreamEncoder* encoder1;
  Http::StreamEncoder* encoder2;
  IntegrationStreamDecoderPtr response1(new IntegrationStreamDecoder(*dispatcher_));
  IntegrationStreamDecoderPtr response2(new IntegrationStreamDecoder(*dispatcher_));
  FakeStreamPtr upstream_request1;
  FakeStreamPtr upstream_request2;
  executeActions(
      {[&]() -> void {
        codec_client = makeHttpConnection(lookupPort("http"), Http::CodecClient::Type::HTTP2);
      },
       // Start request 1
       [&]() -> void {
         encoder1 = &codec_client->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                        {":path", "/test/long/url"},
                                                                        {":scheme", "http"},
                                                                        {":authority", "host"}},
                                                *response1);
       },
       [&]() -> void {
         fake_upstream_connection1 = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
       },
       [&]() -> void { upstream_request1 = fake_upstream_connection1->waitForNewStream(); },

       // Start request 2
       [&]() -> void {
         response2.reset(new IntegrationStreamDecoder(*dispatcher_));
         encoder2 = &codec_client->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                        {":path", "/test/long/url"},
                                                                        {":scheme", "http"},
                                                                        {":authority", "host"}},
                                                *response2);
       },
       [&]() -> void {
         fake_upstream_connection2 = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
       },
       [&]() -> void { upstream_request2 = fake_upstream_connection2->waitForNewStream(); },

       // Finish request 1
       [&]() -> void {
         codec_client->sendData(*encoder1, 1024, true);

       },
       [&]() -> void { upstream_request1->waitForEndStream(*dispatcher_); },

       // Finish request 2
       [&]() -> void {
         codec_client->sendData(*encoder2, 512, true);

       },
       [&]() -> void { upstream_request2->waitForEndStream(*dispatcher_); },

       // Respond request 2
       [&]() -> void {
         upstream_request2->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
         upstream_request2->encodeData(1024, true);
       },
       [&]() -> void {
         response2->waitForEndStream();
         EXPECT_TRUE(upstream_request2->complete());
         EXPECT_EQ(512U, upstream_request2->bodyLength());

         EXPECT_TRUE(response2->complete());
         EXPECT_STREQ("200", response2->headers().Status()->value().c_str());
         EXPECT_EQ(1024U, response2->body().size());
       },

       // Respond request 1
       [&]() -> void {
         upstream_request1->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
         upstream_request1->encodeData(512, true);
       },
       [&]() -> void {
         response1->waitForEndStream();
         EXPECT_TRUE(upstream_request1->complete());
         EXPECT_EQ(1024U, upstream_request1->bodyLength());

         EXPECT_TRUE(response1->complete());
         EXPECT_STREQ("200", response1->headers().Status()->value().c_str());
         EXPECT_EQ(512U, response1->body().size());
       },

       // Cleanup both downstream and upstream
       [&]() -> void { codec_client->close(); },
       [&]() -> void { fake_upstream_connection1->close(); },
       [&]() -> void { fake_upstream_connection1->waitForDisconnect(); },
       [&]() -> void { fake_upstream_connection2->close(); },
       [&]() -> void { fake_upstream_connection2->waitForDisconnect(); }});
}
} // Envoy
