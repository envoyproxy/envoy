#include "http2_integration_test.h"
#include "utility.h"

#include "common/buffer/buffer_impl.h"
#include "common/http/header_map_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

TEST_F(Http2IntegrationTest, RouterNotFound) { testRouterNotFound(Http::CodecClient::Type::HTTP2); }

TEST_F(Http2IntegrationTest, RouterNotFoundBodyNoBuffer) {
  testRouterNotFoundWithBody(HTTP_PORT, Http::CodecClient::Type::HTTP2);
}

TEST_F(Http2IntegrationTest, RouterNotFoundBodyBuffer) {
  testRouterNotFoundWithBody(HTTP_BUFFER_PORT, Http::CodecClient::Type::HTTP2);
}

TEST_F(Http2IntegrationTest, RouterRedirect) { testRouterRedirect(Http::CodecClient::Type::HTTP2); }

TEST_F(Http2IntegrationTest, DrainClose) { testDrainClose(Http::CodecClient::Type::HTTP2); }

TEST_F(Http2IntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(IntegrationTest::HTTP_PORT),
                                       Http::CodecClient::Type::HTTP2, 1024, 512);
}

TEST_F(Http2IntegrationTest, RouterRequestAndResponseWithBodyBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(IntegrationTest::HTTP_BUFFER_PORT),
                                       Http::CodecClient::Type::HTTP2, 1024, 512);
}

TEST_F(Http2IntegrationTest, RouterRequestAndResponseWithGiantBodyBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(IntegrationTest::HTTP_BUFFER_PORT),
                                       Http::CodecClient::Type::HTTP2, 1024 * 1024, 1024 * 1024);
}

TEST_F(Http2IntegrationTest, RouterHeaderOnlyRequestAndResponseNoBuffer) {
  testRouterHeaderOnlyRequestAndResponse(makeClientConnection(IntegrationTest::HTTP_PORT),
                                         Http::CodecClient::Type::HTTP2);
}

TEST_F(Http2IntegrationTest, RouterHeaderOnlyRequestAndResponseBuffer) {
  testRouterHeaderOnlyRequestAndResponse(makeClientConnection(IntegrationTest::HTTP_BUFFER_PORT),
                                         Http::CodecClient::Type::HTTP2);
}

TEST_F(Http2IntegrationTest, RouterUpstreamDisconnectBeforeRequestcomplete) {
  testRouterUpstreamDisconnectBeforeRequestComplete(
      makeClientConnection(IntegrationTest::HTTP_PORT), Http::CodecClient::Type::HTTP2);
}

TEST_F(Http2IntegrationTest, RouterUpstreamDisconnectBeforeResponseComplete) {
  testRouterUpstreamDisconnectBeforeResponseComplete(
      makeClientConnection(IntegrationTest::HTTP_PORT), Http::CodecClient::Type::HTTP2);
}

TEST_F(Http2IntegrationTest, RouterDownstreamDisconnectBeforeRequestComplete) {
  testRouterDownstreamDisconnectBeforeRequestComplete(
      makeClientConnection(IntegrationTest::HTTP_PORT), Http::CodecClient::Type::HTTP2);
}

TEST_F(Http2IntegrationTest, RouterDownstreamDisconnectBeforeResponseComplete) {
  testRouterDownstreamDisconnectBeforeResponseComplete(
      makeClientConnection(IntegrationTest::HTTP_PORT), Http::CodecClient::Type::HTTP2);
}

TEST_F(Http2IntegrationTest, RouterUpstreamResponseBeforeRequestComplete) {
  testRouterUpstreamResponseBeforeRequestComplete(makeClientConnection(IntegrationTest::HTTP_PORT),
                                                  Http::CodecClient::Type::HTTP2);
}

TEST_F(Http2IntegrationTest, TwoRequests) { testTwoRequests(Http::CodecClient::Type::HTTP2); }

TEST_F(Http2IntegrationTest, Retry) { testRetry(Http::CodecClient::Type::HTTP2); }

TEST_F(Http2IntegrationTest, MaxHeadersInCodec) {
  Http::HeaderMapImpl big_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};

  big_headers.addViaMove("big", std::string(63 * 1024, 'a'));

  IntegrationCodecClientPtr codec_client;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  Http::StreamEncoder* downstream_request{};
  executeActions(
      {[&]() -> void {
        codec_client = makeHttpConnection(HTTP_PORT, Http::CodecClient::Type::HTTP2);
      },
       [&]() -> void { downstream_request = &codec_client->startRequest(big_headers, *response); },
       [&]() -> void { response->waitForReset(); }, [&]() -> void { codec_client->close(); }});
}

TEST_F(Http2IntegrationTest, MaxHeadersInConnectionManager) {
  Http::HeaderMapImpl big_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};

  big_headers.addViaMove("big", std::string(60 * 1024, 'a'));

  IntegrationCodecClientPtr codec_client;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  executeActions({[&]() -> void {
    codec_client = makeHttpConnection(HTTP_PORT, Http::CodecClient::Type::HTTP2);
  },
                  [&]() -> void { codec_client->makeHeaderOnlyRequest(big_headers, *response); },
                  [&]() -> void { response->waitForEndStream(); },
                  [&]() -> void { codec_client->close(); }});

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("400", response->headers().get(":status"));
}

TEST_F(Http2IntegrationTest, DownstreamResetBeforeResponseComplete) {
  testDownstreamResetBeforeResponseComplete();
}

void BaseIntegrationTest::testDownstreamResetBeforeResponseComplete() {
  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  Http::StreamEncoder* downstream_request{};
  FakeStreamPtr upstream_request;
  std::list<std::function<void()>> actions = {
      [&]()
          -> void { codec_client = makeHttpConnection(HTTP_PORT, Http::CodecClient::Type::HTTP2); },
      [&]() -> void {
        downstream_request =
            &codec_client->startRequest(Http::HeaderMapImpl{{":method", "GET"},
                                                            {":path", "/test/long/url"},
                                                            {":scheme", "http"},
                                                            {":authority", "host"},
                                                            {"cookie", "a=b"},
                                                            {"cookie", "c=d"}},
                                        *response);
        codec_client->sendData(*downstream_request, 0, true);
      },
      [&]() -> void {
        fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
      },
      [&]() -> void { upstream_request = fake_upstream_connection->waitForNewStream(); },
      [&]() -> void {
        upstream_request->waitForEndStream(*dispatcher_);
        EXPECT_EQ(upstream_request->headers().get("cookie"), "a=b; c=d");
      },
      [&]() -> void {
        upstream_request->encodeHeaders(Http::HeaderMapImpl{{":status", "200"}}, false);
        upstream_request->encodeData(512, false);
      },
      [&]() -> void { response->waitForBodyData(512); },
      [&]() -> void { codec_client->sendReset(*downstream_request); }};

  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP1) {
    actions.push_back([&]() -> void { fake_upstream_connection->waitForDisconnect(); });
  } else {
    actions.push_back([&]() -> void { upstream_request->waitForReset(); });
    actions.push_back([&]() -> void { fake_upstream_connection->close(); });
    actions.push_back([&]() -> void { fake_upstream_connection->waitForDisconnect(); });
  }

  actions.push_back([&]() -> void { codec_client->close(); });
  executeActions(actions);

  EXPECT_TRUE(upstream_request->complete());
  EXPECT_EQ(0U, upstream_request->bodyLength());

  EXPECT_FALSE(response->complete());
  EXPECT_EQ("200", response->headers().get(":status"));
  EXPECT_EQ(512U, response->body().size());
}

TEST_F(Http2IntegrationTest, BadMagic) {
  Buffer::OwnedImpl buffer("hello");
  std::string response;
  RawConnectionDriver connection(
      HTTP_PORT, buffer, [&](Network::ClientConnection&, const Buffer::Instance& data)
                             -> void { response.append(TestUtility::bufferToString(data)); });

  connection.run();
  EXPECT_EQ("", response);
}

TEST_F(Http2IntegrationTest, BadFrame) {
  Buffer::OwnedImpl buffer("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\nhelloworldcauseanerror");
  std::string response;
  RawConnectionDriver connection(
      HTTP_PORT, buffer, [&](Network::ClientConnection&, const Buffer::Instance& data)
                             -> void { response.append(TestUtility::bufferToString(data)); });

  connection.run();
  EXPECT_TRUE(response.find("SETTINGS expected") != std::string::npos);
}

TEST_F(Http2IntegrationTest, GoAway) {
  IntegrationCodecClientPtr codec_client;
  Http::StreamEncoder* encoder;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  executeActions(
      {[&]() -> void {
        codec_client = makeHttpConnection(HTTP_PORT, Http::CodecClient::Type::HTTP2);
      },
       [&]() -> void {
         encoder = &codec_client->startRequest(Http::HeaderMapImpl{{":method", "GET"},
                                                                   {":path", "/healthcheck"},
                                                                   {":scheme", "http"},
                                                                   {":authority", "host"}},
                                               *response);
       },
       [&]() -> void { codec_client->goAway(); },
       [&]() -> void { codec_client->sendData(*encoder, 0, true); },
       [&]() -> void { response->waitForEndStream(); }, [&]() -> void { codec_client->close(); }});

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().get(":status"));
}

void BaseIntegrationTest::testTrailers(uint64_t request_size, uint64_t response_size) {
  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  Http::StreamEncoder* request_encoder;
  FakeStreamPtr upstream_request;
  Http::HeaderMapImpl request_trailers{{"request1", "trailer1"}, {"request2", "trailer2"}};
  Http::HeaderMapImpl response_trailers{{"response1", "trailer1"}, {"response2", "trailer2"}};
  executeActions(
      {[&]() -> void {
        codec_client = makeHttpConnection(HTTP_BUFFER_PORT, Http::CodecClient::Type::HTTP2);
      },
       [&]() -> void {
         request_encoder =
             &codec_client->startRequest(Http::HeaderMapImpl{{":method", "POST"},
                                                             {":path", "/test/long/url"},
                                                             {":scheme", "http"},
                                                             {":authority", "host"}},
                                         *response);
         codec_client->sendData(*request_encoder, request_size, false);
         codec_client->sendTrailers(*request_encoder, request_trailers);
       },
       [&]() -> void {
         fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
       },
       [&]() -> void { upstream_request = fake_upstream_connection->waitForNewStream(); },
       [&]() -> void { upstream_request->waitForEndStream(*dispatcher_); },
       [&]() -> void {
         upstream_request->encodeHeaders(Http::HeaderMapImpl{{":status", "200"}}, false);
         upstream_request->encodeData(response_size, false);
         upstream_request->encodeTrailers(response_trailers);
       },
       [&]() -> void { response->waitForEndStream(); },
       // Cleanup both downstream and upstream
       [&]() -> void { codec_client->close(); },
       [&]() -> void { fake_upstream_connection->close(); },
       [&]() -> void { fake_upstream_connection->waitForDisconnect(); }});

  EXPECT_TRUE(upstream_request->complete());
  EXPECT_EQ(request_size, upstream_request->bodyLength());
  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP2) {
    EXPECT_THAT(*upstream_request->trailers(), HeaderMapEqualRef(request_trailers));
  }

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().get(":status"));
  EXPECT_EQ(response_size, response->body().size());
  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP2) {
    EXPECT_THAT(*response->trailers(), HeaderMapEqualRef(response_trailers));
  }
}

TEST_F(Http2IntegrationTest, Trailers) { testTrailers(1024, 2048); }

TEST_F(Http2IntegrationTest, TrailersGiantBody) { testTrailers(1024 * 1024, 1024 * 1024); }

TEST_F(Http2IntegrationTest, SimultaneousRequest) {
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
        codec_client =
            makeHttpConnection(IntegrationTest::HTTP_PORT, Http::CodecClient::Type::HTTP2);
      },
       // Start request 1
       [&]() -> void {
         encoder1 = &codec_client->startRequest(Http::HeaderMapImpl{{":method", "POST"},
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
         encoder2 = &codec_client->startRequest(Http::HeaderMapImpl{{":method", "POST"},
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
         upstream_request2->encodeHeaders(Http::HeaderMapImpl{{":status", "200"}}, false);
         upstream_request2->encodeData(1024, true);
       },
       [&]() -> void {
         response2->waitForEndStream();
         EXPECT_TRUE(upstream_request2->complete());
         EXPECT_EQ(512U, upstream_request2->bodyLength());

         EXPECT_TRUE(response2->complete());
         EXPECT_EQ("200", response2->headers().get(":status"));
         EXPECT_EQ(1024U, response2->body().size());
       },

       // Respond request 1
       [&]() -> void {
         upstream_request1->encodeHeaders(Http::HeaderMapImpl{{":status", "200"}}, false);
         upstream_request1->encodeData(512, true);
       },
       [&]() -> void {
         response1->waitForEndStream();
         EXPECT_TRUE(upstream_request1->complete());
         EXPECT_EQ(1024U, upstream_request1->bodyLength());

         EXPECT_TRUE(response1->complete());
         EXPECT_EQ("200", response1->headers().get(":status"));
         EXPECT_EQ(512U, response1->body().size());
       },

       // Cleanup both downstream and upstream
       [&]() -> void { codec_client->close(); },
       [&]() -> void { fake_upstream_connection1->close(); },
       [&]() -> void { fake_upstream_connection1->waitForDisconnect(); },
       [&]() -> void { fake_upstream_connection2->close(); },
       [&]() -> void { fake_upstream_connection2->waitForDisconnect(); }});
}
