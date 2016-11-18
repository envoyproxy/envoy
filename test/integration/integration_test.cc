#include "integration.h"
#include "utility.h"

#include "common/buffer/buffer_impl.h"
#include "common/http/header_map_impl.h"

#include "test/test_common/utility.h"

TEST_F(IntegrationTest, Echo) {
  Buffer::OwnedImpl buffer("hello");
  std::string response;
  RawConnectionDriver connection(
      ECHO_PORT, buffer, [&](Network::ClientConnection&, const Buffer::Instance& data) -> void {
        response.append(TestUtility::bufferToString(data));
        connection.close();
      });

  connection.run();
  EXPECT_EQ("hello", response);
}

TEST_F(IntegrationTest, RouterNotFound) { testRouterNotFound(Http::CodecClient::Type::HTTP1); }

void BaseIntegrationTest::testRouterNotFound(Http::CodecClient::Type type) {
  BufferingStreamDecoderPtr response =
      IntegrationUtil::makeSingleRequest(HTTP_PORT, "GET", "/notfound", "", type);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("404", response->headers().Status()->value().c_str());
}

TEST_F(IntegrationTest, RouterNotFoundBodyNoBuffer) {
  testRouterNotFoundWithBody(HTTP_PORT, Http::CodecClient::Type::HTTP1);
}

TEST_F(IntegrationTest, RouterNotFoundBodyBuffer) {
  testRouterNotFoundWithBody(HTTP_BUFFER_PORT, Http::CodecClient::Type::HTTP1);
}

void BaseIntegrationTest::testRouterNotFoundWithBody(uint32_t port, Http::CodecClient::Type type) {
  BufferingStreamDecoderPtr response =
      IntegrationUtil::makeSingleRequest(port, "POST", "/notfound", "foo", type);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("404", response->headers().Status()->value().c_str());
}

TEST_F(IntegrationTest, RouterRedirect) { testRouterRedirect(Http::CodecClient::Type::HTTP1); }

void BaseIntegrationTest::testRouterRedirect(Http::CodecClient::Type type) {
  BufferingStreamDecoderPtr response =
      IntegrationUtil::makeSingleRequest(HTTP_PORT, "GET", "/foo", "", type, "www.redirect.com");
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("301", response->headers().Status()->value().c_str());
  EXPECT_STREQ("https://www.redirect.com/foo",
               response->headers().get(Http::Headers::get().Location)->value().c_str());
}

TEST_F(IntegrationTest, DrainClose) { testDrainClose(Http::CodecClient::Type::HTTP1); }

void BaseIntegrationTest::testDrainClose(Http::CodecClient::Type type) {
  test_server_->drainManager().draining_ = true;

  IntegrationCodecClientPtr codec_client;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  executeActions({[&]() -> void { codec_client = makeHttpConnection(HTTP_PORT, type); },
                  [&]() -> void {
                    codec_client->makeHeaderOnlyRequest(
                        Http::TestHeaderMapImpl{{":method", "GET"},
                                                {":path", "/healthcheck"},
                                                {":scheme", "http"},
                                                {":authority", "host"}},
                        *response);
                  },
                  [&]() -> void { response->waitForEndStream(); },
                  [&]() -> void { codec_client->waitForDisconnect(); }});

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  if (type == Http::CodecClient::Type::HTTP2) {
    EXPECT_TRUE(codec_client->sawGoAway());
  }

  test_server_->drainManager().draining_ = false;
}

TEST_F(IntegrationTest, ConnectionClose) {
  IntegrationCodecClientPtr codec_client;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  executeActions({[&]() -> void {
    codec_client = makeHttpConnection(HTTP_PORT, Http::CodecClient::Type::HTTP1);
  },
                  [&]() -> void {
                    codec_client->makeHeaderOnlyRequest(
                        Http::TestHeaderMapImpl{{":method", "GET"},
                                                {":path", "/healthcheck"},
                                                {":authority", "host"},
                                                {"connection", "close"}},
                        *response);
                  },
                  [&]() -> void { response->waitForEndStream(); },
                  [&]() -> void { codec_client->waitForDisconnect(); }});

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}

void BaseIntegrationTest::testRouterRequestAndResponseWithBody(Network::ClientConnectionPtr&& conn,
                                                               Http::CodecClient::Type type,
                                                               uint64_t request_size,
                                                               uint64_t response_size,
                                                               bool big_header) {
  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  FakeStreamPtr request;
  executeActions({[&]() -> void { codec_client = makeHttpConnection(std::move(conn), type); },
                  [&]() -> void {
                    Http::TestHeaderMapImpl headers{{":method", "POST"},
                                                    {":path", "/test/long/url"},
                                                    {":scheme", "http"},
                                                    {":authority", "host"},
                                                    {"x-lyft-user-id", "123"},
                                                    {"x-forwarded-for", "10.0.0.1"}};
                    if (big_header) {
                      headers.addViaCopy("big", std::string(4096, 'a'));
                    }

                    codec_client->makeRequestWithBody(headers, request_size, *response);
                  },
                  [&]() -> void {
                    fake_upstream_connection =
                        fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
                  },
                  [&]() -> void { request = fake_upstream_connection->waitForNewStream(); },
                  [&]() -> void { request->waitForEndStream(*dispatcher_); },
                  [&]() -> void {
                    request->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
                    request->encodeData(response_size, true);
                  },
                  [&]() -> void { response->waitForEndStream(); },
                  // Cleanup both downstream and upstream
                  [&]() -> void { codec_client->close(); },
                  [&]() -> void { fake_upstream_connection->close(); },
                  [&]() -> void { fake_upstream_connection->waitForDisconnect(); }});

  EXPECT_TRUE(request->complete());
  EXPECT_EQ(request_size, request->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(response_size, response->body().size());
}

TEST_F(IntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(IntegrationTest::HTTP_PORT),
                                       Http::CodecClient::Type::HTTP1, 1024, 512, false);
}

TEST_F(IntegrationTest, RouterRequestAndResponseWithBodyBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(IntegrationTest::HTTP_BUFFER_PORT),
                                       Http::CodecClient::Type::HTTP1, 1024, 512, false);
}

TEST_F(IntegrationTest, RouterRequestAndResponseWithGiantBodyBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(IntegrationTest::HTTP_BUFFER_PORT),
                                       Http::CodecClient::Type::HTTP1, 4 * 1024 * 1024,
                                       4 * 1024 * 1024, false);
}

TEST_F(IntegrationTest, RouterRequestAndResponseLargeHeaderNoBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(IntegrationTest::HTTP_PORT),
                                       Http::CodecClient::Type::HTTP1, 1024, 512, true);
}

void BaseIntegrationTest::testRouterHeaderOnlyRequestAndResponse(
    Network::ClientConnectionPtr&& conn, Http::CodecClient::Type type) {

  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  FakeStreamPtr request;
  executeActions(
      {[&]() -> void { codec_client = makeHttpConnection(std::move(conn), type); },
       [&]() -> void {
         codec_client->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                     {":path", "/test/long/url"},
                                                                     {":scheme", "http"},
                                                                     {":authority", "host"},
                                                                     {"x-lyft-user-id", "123"}},
                                             *response);
       },
       [&]() -> void {
         fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
       },
       [&]() -> void { request = fake_upstream_connection->waitForNewStream(); },
       [&]() -> void { request->waitForEndStream(*dispatcher_); },
       [&]() -> void {
         request->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, true);
       },
       [&]() -> void { response->waitForEndStream(); },
       // Cleanup both downstream and upstream
       [&]() -> void { codec_client->close(); },
       [&]() -> void { fake_upstream_connection->close(); },
       [&]() -> void { fake_upstream_connection->waitForDisconnect(); }});

  EXPECT_TRUE(request->complete());
  EXPECT_EQ(0U, request->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(0U, response->body().size());
}

TEST_F(IntegrationTest, RouterHeaderOnlyRequestAndResponseNoBuffer) {
  testRouterHeaderOnlyRequestAndResponse(makeClientConnection(IntegrationTest::HTTP_PORT),
                                         Http::CodecClient::Type::HTTP1);
}

TEST_F(IntegrationTest, RouterHeaderOnlyRequestAndResponseBuffer) {
  testRouterHeaderOnlyRequestAndResponse(makeClientConnection(IntegrationTest::HTTP_BUFFER_PORT),
                                         Http::CodecClient::Type::HTTP1);
}

TEST_F(IntegrationTest, RouterUpstreamDisconnectBeforeRequestcomplete) {
  testRouterUpstreamDisconnectBeforeRequestComplete(
      makeClientConnection(IntegrationTest::HTTP_PORT), Http::CodecClient::Type::HTTP1);
}

void BaseIntegrationTest::testRouterUpstreamDisconnectBeforeRequestComplete(
    Network::ClientConnectionPtr&& conn, Http::CodecClient::Type type) {
  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  FakeStreamPtr request;
  std::list<std::function<void()>> actions = {
      [&]() -> void { codec_client = makeHttpConnection(std::move(conn), type); },
      [&]() -> void {
        codec_client->startRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                           {":path", "/test/long/url"},
                                                           {":scheme", "http"},
                                                           {":authority", "host"}},
                                   *response);
      },
      [&]() -> void {
        fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
      },
      [&]() -> void { request = fake_upstream_connection->waitForNewStream(); },
      [&]() -> void { request->waitForHeadersComplete(); },
      [&]() -> void { fake_upstream_connection->close(); },
      [&]() -> void { fake_upstream_connection->waitForDisconnect(); },
      [&]() -> void { response->waitForEndStream(); }};

  if (type == Http::CodecClient::Type::HTTP1) {
    actions.push_back([&]() -> void { codec_client->waitForDisconnect(); });
  } else {
    actions.push_back([&]() -> void { codec_client->close(); });
  }

  executeActions(actions);

  EXPECT_FALSE(request->complete());
  EXPECT_EQ(0U, request->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("503", response->headers().Status()->value().c_str());
  EXPECT_EQ("upstream connect error or disconnect/reset before headers", response->body());
}

TEST_F(IntegrationTest, RouterUpstreamDisconnectBeforeResponseComplete) {
  testRouterUpstreamDisconnectBeforeResponseComplete(
      makeClientConnection(IntegrationTest::HTTP_PORT), Http::CodecClient::Type::HTTP1);
}

void BaseIntegrationTest::testRouterUpstreamDisconnectBeforeResponseComplete(
    Network::ClientConnectionPtr&& conn, Http::CodecClient::Type type) {
  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  FakeStreamPtr request;
  std::list<std::function<void()>> actions = {
      [&]() -> void { codec_client = makeHttpConnection(std::move(conn), type); },
      [&]() -> void {
        codec_client->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                    {":path", "/test/long/url"},
                                                                    {":scheme", "http"},
                                                                    {":authority", "host"}},
                                            *response);
      },
      [&]() -> void {
        fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
      },
      [&]() -> void { request = fake_upstream_connection->waitForNewStream(); },
      [&]() -> void { request->waitForEndStream(*dispatcher_); },
      [&]() -> void {
        request->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
      },
      [&]() -> void { fake_upstream_connection->close(); },
      [&]() -> void { fake_upstream_connection->waitForDisconnect(); }};

  if (type == Http::CodecClient::Type::HTTP1) {
    actions.push_back([&]() -> void { codec_client->waitForDisconnect(); });
  } else {
    actions.push_back([&]() -> void { response->waitForReset(); });
    actions.push_back([&]() -> void { codec_client->close(); });
  }

  executeActions(actions);

  EXPECT_TRUE(request->complete());
  EXPECT_EQ(0U, request->bodyLength());

  EXPECT_FALSE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(0U, response->body().size());
}

TEST_F(IntegrationTest, RouterDownstreamDisconnectBeforeRequestComplete) {
  testRouterDownstreamDisconnectBeforeRequestComplete(
      makeClientConnection(IntegrationTest::HTTP_PORT), Http::CodecClient::Type::HTTP1);
}

void BaseIntegrationTest::testRouterDownstreamDisconnectBeforeRequestComplete(
    Network::ClientConnectionPtr&& conn, Http::CodecClient::Type type) {
  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  FakeStreamPtr request;
  std::list<std::function<void()>> actions = {
      [&]() -> void { codec_client = makeHttpConnection(std::move(conn), type); },
      [&]() -> void {
        codec_client->startRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                           {":path", "/test/long/url"},
                                                           {":scheme", "http"},
                                                           {":authority", "host"}},
                                   *response);
      },
      [&]() -> void {
        fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
      },
      [&]() -> void { request = fake_upstream_connection->waitForNewStream(); },
      [&]() -> void { request->waitForHeadersComplete(); },
      [&]() -> void { codec_client->close(); }};

  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP1) {
    actions.push_back([&]() -> void { fake_upstream_connection->waitForDisconnect(); });
  } else {
    actions.push_back([&]() -> void { request->waitForReset(); });
    actions.push_back([&]() -> void { fake_upstream_connection->close(); });
    actions.push_back([&]() -> void { fake_upstream_connection->waitForDisconnect(); });
  }

  executeActions(actions);

  EXPECT_FALSE(request->complete());
  EXPECT_EQ(0U, request->bodyLength());

  EXPECT_FALSE(response->complete());
}

TEST_F(IntegrationTest, RouterDownstreamDisconnectBeforeResponseComplete) {
  testRouterDownstreamDisconnectBeforeResponseComplete(
      makeClientConnection(IntegrationTest::HTTP_PORT), Http::CodecClient::Type::HTTP1);
}

void BaseIntegrationTest::testRouterDownstreamDisconnectBeforeResponseComplete(
    Network::ClientConnectionPtr&& conn, Http::CodecClient::Type type) {
  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  FakeStreamPtr request;
  std::list<std::function<void()>> actions = {
      [&]() -> void { codec_client = makeHttpConnection(std::move(conn), type); },
      [&]() -> void {
        codec_client->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                    {":path", "/test/long/url"},
                                                                    {":scheme", "http"},
                                                                    {":authority", "host"}},
                                            *response);
      },
      [&]() -> void {
        fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
      },
      [&]() -> void { request = fake_upstream_connection->waitForNewStream(); },
      [&]() -> void { request->waitForEndStream(*dispatcher_); },
      [&]() -> void {
        request->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
        request->encodeData(512, false);
      },
      [&]() -> void { response->waitForBodyData(512); }, [&]() -> void { codec_client->close(); }};

  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP1) {
    actions.push_back([&]() -> void { fake_upstream_connection->waitForDisconnect(); });
  } else {
    actions.push_back([&]() -> void { request->waitForReset(); });
    actions.push_back([&]() -> void { fake_upstream_connection->close(); });
    actions.push_back([&]() -> void { fake_upstream_connection->waitForDisconnect(); });
  }

  executeActions(actions);

  EXPECT_TRUE(request->complete());
  EXPECT_EQ(0U, request->bodyLength());

  EXPECT_FALSE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(512U, response->body().size());
}

TEST_F(IntegrationTest, RouterUpstreamResponseBeforeRequestComplete) {
  testRouterUpstreamResponseBeforeRequestComplete(makeClientConnection(IntegrationTest::HTTP_PORT),
                                                  Http::CodecClient::Type::HTTP1);
}

void BaseIntegrationTest::testRouterUpstreamResponseBeforeRequestComplete(
    Network::ClientConnectionPtr&& conn, Http::CodecClient::Type type) {
  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  FakeStreamPtr request;
  std::list<std::function<void()>> actions = {
      [&]() -> void { codec_client = makeHttpConnection(std::move(conn), type); },
      [&]() -> void {
        codec_client->startRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                           {":path", "/test/long/url"},
                                                           {":scheme", "http"},
                                                           {":authority", "host"}},
                                   *response);
      },
      [&]() -> void {
        fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
      },
      [&]() -> void { request = fake_upstream_connection->waitForNewStream(); },
      [&]() -> void { request->waitForHeadersComplete(); },
      [&]() -> void {
        request->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
        request->encodeData(512, true);
      },
      [&]() -> void { response->waitForEndStream(); }};

  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP1) {
    actions.push_back([&]() -> void { fake_upstream_connection->waitForDisconnect(); });
  } else {
    actions.push_back([&]() -> void { request->waitForReset(); });
    actions.push_back([&]() -> void { fake_upstream_connection->close(); });
    actions.push_back([&]() -> void { fake_upstream_connection->waitForDisconnect(); });
  }

  if (type == Http::CodecClient::Type::HTTP1) {
    actions.push_back([&]() -> void { codec_client->waitForDisconnect(); });
  } else {
    actions.push_back([&]() -> void { codec_client->close(); });
  }

  executeActions(actions);

  EXPECT_FALSE(request->complete());
  EXPECT_EQ(0U, request->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(512U, response->body().size());
}

TEST_F(IntegrationTest, Retry) { testRetry(Http::CodecClient::Type::HTTP1); }

void BaseIntegrationTest::testRetry(Http::CodecClient::Type type) {
  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  FakeStreamPtr request;
  executeActions(
      {[&]() -> void { codec_client = makeHttpConnection(IntegrationTest::HTTP_PORT, type); },
       [&]() -> void {
         codec_client->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                   {":path", "/test/long/url"},
                                                                   {":scheme", "http"},
                                                                   {":authority", "host"},
                                                                   {"x-forwarded-for", "10.0.0.1"},
                                                                   {"x-envoy-retry-on", "5xx"}},
                                           1024, *response);
       },
       [&]() -> void {
         fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
       },
       [&]() -> void { request = fake_upstream_connection->waitForNewStream(); },
       [&]() -> void { request->waitForEndStream(*dispatcher_); },
       [&]() -> void {
         request->encodeHeaders(Http::TestHeaderMapImpl{{":status", "503"}}, false);
       },
       [&]() -> void {
         if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP1) {
           fake_upstream_connection->waitForDisconnect();
           fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
         } else {
           request->waitForReset();
         }
       },
       [&]() -> void { request = fake_upstream_connection->waitForNewStream(); },
       [&]() -> void { request->waitForEndStream(*dispatcher_); },
       [&]() -> void {
         request->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
         request->encodeData(512, true);
       },
       [&]() -> void {
         response->waitForEndStream();
         EXPECT_TRUE(request->complete());
         EXPECT_EQ(1024U, request->bodyLength());

         EXPECT_TRUE(response->complete());
         EXPECT_STREQ("200", response->headers().Status()->value().c_str());
         EXPECT_EQ(512U, response->body().size());
       },
       // Cleanup both downstream and upstream
       [&]() -> void { codec_client->close(); },
       [&]() -> void { fake_upstream_connection->close(); },
       [&]() -> void { fake_upstream_connection->waitForDisconnect(); }});
}

TEST_F(IntegrationTest, TwoRequests) { testTwoRequests(Http::CodecClient::Type::HTTP1); }

void BaseIntegrationTest::testTwoRequests(Http::CodecClient::Type type) {
  IntegrationCodecClientPtr codec_client;
  FakeHttpConnectionPtr fake_upstream_connection;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  FakeStreamPtr request;
  executeActions(
      {[&]() -> void { codec_client = makeHttpConnection(IntegrationTest::HTTP_PORT, type); },
       // Request 1.
       [&]() -> void {
         codec_client->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                   {":path", "/test/long/url"},
                                                                   {":scheme", "http"},
                                                                   {":authority", "host"}},
                                           1024, *response);
       },
       [&]() -> void {
         fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
       },
       [&]() -> void { request = fake_upstream_connection->waitForNewStream(); },
       [&]() -> void { request->waitForEndStream(*dispatcher_); },
       [&]() -> void {
         request->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
         request->encodeData(512, true);
       },
       [&]() -> void {
         response->waitForEndStream();
         EXPECT_TRUE(request->complete());
         EXPECT_EQ(1024U, request->bodyLength());

         EXPECT_TRUE(response->complete());
         EXPECT_STREQ("200", response->headers().Status()->value().c_str());
         EXPECT_EQ(512U, response->body().size());
       },
       // Request 2.
       [&]() -> void {
         response.reset(new IntegrationStreamDecoder(*dispatcher_));
         codec_client->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                   {":path", "/test/long/url"},
                                                                   {":scheme", "http"},
                                                                   {":authority", "host"}},
                                           512, *response);
       },
       [&]() -> void { request = fake_upstream_connection->waitForNewStream(); },
       [&]() -> void { request->waitForEndStream(*dispatcher_); },
       [&]() -> void {
         request->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
         request->encodeData(1024, true);
       },
       [&]() -> void {
         response->waitForEndStream();
         EXPECT_TRUE(request->complete());
         EXPECT_EQ(512U, request->bodyLength());

         EXPECT_TRUE(response->complete());
         EXPECT_STREQ("200", response->headers().Status()->value().c_str());
         EXPECT_EQ(1024U, response->body().size());
       },
       // Cleanup both downstream and upstream
       [&]() -> void { codec_client->close(); },
       [&]() -> void { fake_upstream_connection->close(); },
       [&]() -> void { fake_upstream_connection->waitForDisconnect(); }});
}

TEST_F(IntegrationTest, BadHttpRequest) { testBadHttpRequest(); }

void BaseIntegrationTest::testBadHttpRequest() {
  Buffer::OwnedImpl buffer("hello");
  std::string response;
  RawConnectionDriver connection(
      HTTP_PORT, buffer, [&](Network::ClientConnection&, const Buffer::Instance& data)
                             -> void { response.append(TestUtility::bufferToString(data)); });

  connection.run();
  EXPECT_EQ("HTTP/1.1 400 Bad Request\r\ncontent-length: 0\r\nconnection: close\r\n\r\n", response);
}

TEST_F(IntegrationTest, Http10Request) { testHttp10Request(); }

void BaseIntegrationTest::testHttp10Request() {
  Buffer::OwnedImpl buffer("GET / HTTP/1.0\r\n\r\n");
  std::string response;
  RawConnectionDriver connection(HTTP_PORT, buffer, [&](Network::ClientConnection& client,
                                                        const Buffer::Instance& data) -> void {
    response.append(TestUtility::bufferToString(data));
    client.close(Network::ConnectionCloseType::NoFlush);
  });

  connection.run();
  EXPECT_TRUE(response.find("HTTP/1.1 426 Upgrade Required\r\n") == 0);
}

TEST_F(IntegrationTest, NoHost) { testNoHost(); }

void BaseIntegrationTest::testNoHost() {
  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\n");
  std::string response;
  RawConnectionDriver connection(HTTP_PORT, buffer, [&](Network::ClientConnection& client,
                                                        const Buffer::Instance& data) -> void {
    response.append(TestUtility::bufferToString(data));
    client.close(Network::ConnectionCloseType::NoFlush);
  });

  connection.run();
  EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request\r\n") == 0);
}

TEST_F(IntegrationTest, BadPath) { testBadPath(); }

void BaseIntegrationTest::testBadPath() {
  Buffer::OwnedImpl buffer("GET http://api.lyft.com HTTP/1.1\r\nHost: host\r\n\r\n");
  std::string response;
  RawConnectionDriver connection(HTTP_PORT, buffer, [&](Network::ClientConnection& client,
                                                        const Buffer::Instance& data) -> void {
    response.append(TestUtility::bufferToString(data));
    client.close(Network::ConnectionCloseType::NoFlush);
  });

  connection.run();
  EXPECT_TRUE(response.find("HTTP/1.1 404 Not Found\r\n") == 0);
}

TEST_F(IntegrationTest, UpstreamProtocolError) { testUpstreamProtocolError(); }

void BaseIntegrationTest::testUpstreamProtocolError() {
  IntegrationCodecClientPtr codec_client;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  FakeRawConnectionPtr fake_upstream_connection;
  executeActions({[&]() -> void {
    codec_client = makeHttpConnection(IntegrationTest::HTTP_PORT, Http::CodecClient::Type::HTTP1);
  },
                  [&]() -> void {
                    codec_client->startRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                       {":path", "/test/long/url"},
                                                                       {":authority", "host"}},
                                               *response);
                  },
                  [&]() -> void {
                    fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();
                  },
                  // TODO: Waiting for exact amount of data is a hack. This needs to be fixed.
                  [&]() -> void { fake_upstream_connection->waitForData(187); },
                  [&]() -> void { fake_upstream_connection->write("bad protocol data!"); },
                  [&]() -> void { fake_upstream_connection->waitForDisconnect(); },
                  [&]() -> void { codec_client->waitForDisconnect(); }});

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("503", response->headers().Status()->value().c_str());
}

TEST_F(IntegrationTest, TcpProxyUpstreamDisconnect) {
  IntegrationTcpClientPtr tcp_client;
  FakeRawConnectionPtr fake_upstream_connection;
  executeActions(
      {[&]() -> void { tcp_client = makeTcpConnection(IntegrationTest::TCP_PROXY_PORT); },
       [&]() -> void { tcp_client->write("hello"); },
       [&]() -> void { fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection(); },
       [&]() -> void { fake_upstream_connection->waitForData(5); },
       [&]() -> void { fake_upstream_connection->write("world"); },
       [&]() -> void { fake_upstream_connection->close(); },
       [&]() -> void { fake_upstream_connection->waitForDisconnect(); },
       [&]() -> void { tcp_client->waitForDisconnect(); }});

  EXPECT_EQ("world", tcp_client->data());
}

TEST_F(IntegrationTest, TcpProxyDownstreamDisconnect) {
  IntegrationTcpClientPtr tcp_client;
  FakeRawConnectionPtr fake_upstream_connection;
  executeActions(
      {[&]() -> void { tcp_client = makeTcpConnection(IntegrationTest::TCP_PROXY_PORT); },
       [&]() -> void { tcp_client->write("hello"); },
       [&]() -> void { fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection(); },
       [&]() -> void { fake_upstream_connection->waitForData(5); },
       [&]() -> void { fake_upstream_connection->write("world"); },
       [&]() -> void { tcp_client->waitForData("world"); },
       [&]() -> void { tcp_client->write("hello"); }, [&]() -> void { tcp_client->close(); },
       [&]() -> void { fake_upstream_connection->waitForData(10); },
       [&]() -> void { fake_upstream_connection->waitForDisconnect(); }});
}
