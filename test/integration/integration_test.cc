#include "test/integration/integration_test.h"

#include "common/http/header_map_impl.h"

#include "test/integration/utility.h"
#include "test/test_common/utility.h"

TEST_F(IntegrationTest, Echo) {
  Buffer::OwnedImpl buffer("hello");
  std::string response;
  RawConnectionDriver connection(lookupPort("echo"), buffer,
                                 [&](Network::ClientConnection&, const Buffer::Instance& data)
                                     -> void {
                                       response.append(TestUtility::bufferToString(data));
                                       connection.close();
                                     });

  connection.run();
  EXPECT_EQ("hello", response);
}

TEST_F(IntegrationTest, RouterNotFound) { testRouterNotFound(Http::CodecClient::Type::HTTP1); }

TEST_F(IntegrationTest, RouterNotFoundBodyNoBuffer) {
  testRouterNotFoundWithBody(lookupPort("http"), Http::CodecClient::Type::HTTP1);
}

TEST_F(IntegrationTest, RouterNotFoundBodyBuffer) {
  testRouterNotFoundWithBody(lookupPort("http_buffer"), Http::CodecClient::Type::HTTP1);
}

TEST_F(IntegrationTest, RouterRedirect) { testRouterRedirect(Http::CodecClient::Type::HTTP1); }

TEST_F(IntegrationTest, DrainClose) { testDrainClose(Http::CodecClient::Type::HTTP1); }

TEST_F(IntegrationTest, ConnectionClose) {
  IntegrationCodecClientPtr codec_client;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  executeActions({[&]() -> void {
    codec_client = makeHttpConnection(lookupPort("http"), Http::CodecClient::Type::HTTP1);
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

TEST_F(IntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(lookupPort("http")),
                                       Http::CodecClient::Type::HTTP1, 1024, 512, false);
}

TEST_F(IntegrationTest, RouterRequestAndResponseWithBodyBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(lookupPort("http_buffer")),
                                       Http::CodecClient::Type::HTTP1, 1024, 512, false);
}

TEST_F(IntegrationTest, RouterRequestAndResponseWithGiantBodyBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(lookupPort("http_buffer")),
                                       Http::CodecClient::Type::HTTP1, 4 * 1024 * 1024,
                                       4 * 1024 * 1024, false);
}

TEST_F(IntegrationTest, RouterRequestAndResponseLargeHeaderNoBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(lookupPort("http")),
                                       Http::CodecClient::Type::HTTP1, 1024, 512, true);
}

TEST_F(IntegrationTest, RouterHeaderOnlyRequestAndResponseNoBuffer) {
  testRouterHeaderOnlyRequestAndResponse(makeClientConnection(lookupPort("http")),
                                         Http::CodecClient::Type::HTTP1);
}

TEST_F(IntegrationTest, RouterHeaderOnlyRequestAndResponseBuffer) {
  testRouterHeaderOnlyRequestAndResponse(makeClientConnection(lookupPort("http_buffer")),
                                         Http::CodecClient::Type::HTTP1);
}

TEST_F(IntegrationTest, RouterUpstreamDisconnectBeforeRequestcomplete) {
  testRouterUpstreamDisconnectBeforeRequestComplete(makeClientConnection(lookupPort("http")),
                                                    Http::CodecClient::Type::HTTP1);
}

TEST_F(IntegrationTest, RouterUpstreamDisconnectBeforeResponseComplete) {
  testRouterUpstreamDisconnectBeforeResponseComplete(makeClientConnection(lookupPort("http")),
                                                     Http::CodecClient::Type::HTTP1);
}

TEST_F(IntegrationTest, RouterDownstreamDisconnectBeforeRequestComplete) {
  testRouterDownstreamDisconnectBeforeRequestComplete(makeClientConnection(lookupPort("http")),
                                                      Http::CodecClient::Type::HTTP1);
}

TEST_F(IntegrationTest, RouterDownstreamDisconnectBeforeResponseComplete) {
  testRouterDownstreamDisconnectBeforeResponseComplete(makeClientConnection(lookupPort("http")),
                                                       Http::CodecClient::Type::HTTP1);
}

TEST_F(IntegrationTest, RouterUpstreamResponseBeforeRequestComplete) {
  testRouterUpstreamResponseBeforeRequestComplete(makeClientConnection(lookupPort("http")),
                                                  Http::CodecClient::Type::HTTP1);
}

TEST_F(IntegrationTest, Retry) { testRetry(Http::CodecClient::Type::HTTP1); }

TEST_F(IntegrationTest, TwoRequests) { testTwoRequests(Http::CodecClient::Type::HTTP1); }

TEST_F(IntegrationTest, BadHttpRequest) { testBadHttpRequest(); }

TEST_F(IntegrationTest, Http10Request) { testHttp10Request(); }

TEST_F(IntegrationTest, NoHost) { testNoHost(); }

TEST_F(IntegrationTest, BadPath) { testBadPath(); }

TEST_F(IntegrationTest, UpstreamProtocolError) { testUpstreamProtocolError(); }

TEST_F(IntegrationTest, TcpProxyUpstreamDisconnect) {
  IntegrationTcpClientPtr tcp_client;
  FakeRawConnectionPtr fake_upstream_connection;
  executeActions(
      {[&]() -> void { tcp_client = makeTcpConnection(lookupPort("tcp_proxy")); },
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
      {[&]() -> void { tcp_client = makeTcpConnection(lookupPort("tcp_proxy")); },
       [&]() -> void { tcp_client->write("hello"); },
       [&]() -> void { fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection(); },
       [&]() -> void { fake_upstream_connection->waitForData(5); },
       [&]() -> void { fake_upstream_connection->write("world"); },
       [&]() -> void { tcp_client->waitForData("world"); },
       [&]() -> void { tcp_client->write("hello"); }, [&]() -> void { tcp_client->close(); },
       [&]() -> void { fake_upstream_connection->waitForData(10); },
       [&]() -> void { fake_upstream_connection->waitForDisconnect(); }});
}
