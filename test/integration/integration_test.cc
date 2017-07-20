#include "test/integration/integration_test.h"

#include <string>

#include "common/http/header_map_impl.h"

#include "test/integration/utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

INSTANTIATE_TEST_CASE_P(IpVersions, IntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(IntegrationTest, RouterNotFound) { testRouterNotFound(Http::CodecClient::Type::HTTP1); }

TEST_P(IntegrationTest, RouterNotFoundBodyNoBuffer) {
  testRouterNotFoundWithBody(lookupPort("http"), Http::CodecClient::Type::HTTP1);
}

TEST_P(IntegrationTest, RouterNotFoundBodyBuffer) {
  testRouterNotFoundWithBody(lookupPort("http_buffer"), Http::CodecClient::Type::HTTP1);
}

TEST_P(IntegrationTest, RouterRedirect) { testRouterRedirect(Http::CodecClient::Type::HTTP1); }

TEST_P(IntegrationTest, DrainClose) { testDrainClose(Http::CodecClient::Type::HTTP1); }

TEST_P(IntegrationTest, ConnectionClose) {
  IntegrationCodecClientPtr codec_client;
  IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
  executeActions(
      {[&]() -> void {
         codec_client = makeHttpConnection(lookupPort("http"), Http::CodecClient::Type::HTTP1);
       },
       [&]() -> void {
         codec_client->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{{":method", "GET"},
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

TEST_P(IntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(lookupPort("http")),
                                       Http::CodecClient::Type::HTTP1, 1024, 512, false);
}

TEST_P(IntegrationTest, RouterRequestAndResponseWithBodyBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(lookupPort("http_buffer")),
                                       Http::CodecClient::Type::HTTP1, 1024, 512, false);
}

TEST_P(IntegrationTest, RouterRequestAndResponseWithGiantBodyBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(lookupPort("http_buffer")),
                                       Http::CodecClient::Type::HTTP1, 4 * 1024 * 1024,
                                       4 * 1024 * 1024, false);
}

TEST_P(IntegrationTest, RouterRequestAndResponseLargeHeaderNoBuffer) {
  testRouterRequestAndResponseWithBody(makeClientConnection(lookupPort("http")),
                                       Http::CodecClient::Type::HTTP1, 1024, 512, true);
}

TEST_P(IntegrationTest, RouterHeaderOnlyRequestAndResponseNoBuffer) {
  testRouterHeaderOnlyRequestAndResponse(makeClientConnection(lookupPort("http")),
                                         Http::CodecClient::Type::HTTP1);
}

TEST_P(IntegrationTest, RouterHeaderOnlyRequestAndResponseBuffer) {
  testRouterHeaderOnlyRequestAndResponse(makeClientConnection(lookupPort("http_buffer")),
                                         Http::CodecClient::Type::HTTP1);
}

TEST_P(IntegrationTest, RouterUpstreamDisconnectBeforeRequestcomplete) {
  testRouterUpstreamDisconnectBeforeRequestComplete(makeClientConnection(lookupPort("http")),
                                                    Http::CodecClient::Type::HTTP1);
}

TEST_P(IntegrationTest, RouterUpstreamDisconnectBeforeResponseComplete) {
  testRouterUpstreamDisconnectBeforeResponseComplete(makeClientConnection(lookupPort("http")),
                                                     Http::CodecClient::Type::HTTP1);
}

TEST_P(IntegrationTest, RouterDownstreamDisconnectBeforeRequestComplete) {
  testRouterDownstreamDisconnectBeforeRequestComplete(makeClientConnection(lookupPort("http")),
                                                      Http::CodecClient::Type::HTTP1);
}

TEST_P(IntegrationTest, RouterDownstreamDisconnectBeforeResponseComplete) {
  testRouterDownstreamDisconnectBeforeResponseComplete(makeClientConnection(lookupPort("http")),
                                                       Http::CodecClient::Type::HTTP1);
}

TEST_P(IntegrationTest, RouterUpstreamResponseBeforeRequestComplete) {
  testRouterUpstreamResponseBeforeRequestComplete(makeClientConnection(lookupPort("http")),
                                                  Http::CodecClient::Type::HTTP1);
}

TEST_P(IntegrationTest, Retry) { testRetry(Http::CodecClient::Type::HTTP1); }

TEST_P(IntegrationTest, TwoRequests) { testTwoRequests(Http::CodecClient::Type::HTTP1); }

TEST_P(IntegrationTest, BadFirstline) { testBadFirstline(); }

TEST_P(IntegrationTest, MissingDelimiter) { testMissingDelimiter(); }

TEST_P(IntegrationTest, InvalidCharacterInFirstline) { testInvalidCharacterInFirstline(); }

TEST_P(IntegrationTest, LowVersion) { testLowVersion(); }

TEST_P(IntegrationTest, Http10Request) { testHttp10Request(); }

TEST_P(IntegrationTest, NoHost) { testNoHost(); }

TEST_P(IntegrationTest, BadPath) { testBadPath(); }

TEST_P(IntegrationTest, ValidZeroLengthContent) {
  testValidZeroLengthContent(Http::CodecClient::Type::HTTP1);
}

TEST_P(IntegrationTest, InvalidContentLength) {
  testInvalidContentLength(Http::CodecClient::Type::HTTP1);
}
TEST_P(IntegrationTest, MultipleContentLengths) {
  testMultipleContentLengths(Http::CodecClient::Type::HTTP1);
}

TEST_P(IntegrationTest, OverlyLongHeaders) {
  testOverlyLongHeaders(Http::CodecClient::Type::HTTP1);
}

TEST_P(IntegrationTest, UpstreamProtocolError) { testUpstreamProtocolError(); }

TEST_P(IntegrationTest, WebSocketConnectionDownstreamDisconnect) {
  // WebSocket upgrade, send some data and disconnect downstream
  IntegrationTcpClientPtr tcp_client;
  FakeRawConnectionPtr fake_upstream_connection;
  const std::string upgrade_req_str = "GET /websocket/test HTTP/1.1\r\nHost: host\r\nConnection: "
                                      "Upgrade\r\nUpgrade: websocket\r\n\r\n";
  const std::string upgrade_resp_str =
      "HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n\r\n";
  executeActions({
      [&]() -> void { tcp_client = makeTcpConnection(lookupPort("http")); },
      // Send websocket upgrade request
      [&]() -> void { tcp_client->write(upgrade_req_str); },
      [&]() -> void { fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection(); },
      [&]() -> void { fake_upstream_connection->waitForData(190); },
      // Accept websocket upgrade request
      [&]() -> void { fake_upstream_connection->write(upgrade_resp_str); },
      [&]() -> void { tcp_client->waitForData(upgrade_resp_str); },
      // Standard TCP proxy semantics post upgrade
      [&]() -> void { tcp_client->write("hello"); },
      [&]() -> void { fake_upstream_connection->waitForData(195); },
      [&]() -> void { fake_upstream_connection->write("world"); },
      [&]() -> void { tcp_client->waitForData(upgrade_resp_str + "world"); },
      [&]() -> void { tcp_client->write("bye!"); },
      // downstream disconnect
      [&]() -> void { tcp_client->close(); },
      [&]() -> void { fake_upstream_connection->waitForData(199); },
      [&]() -> void { fake_upstream_connection->waitForDisconnect(); },
  });
}

TEST_P(IntegrationTest, WebSocketConnectionUpstreamDisconnect) {
  // WebSocket upgrade, send some data and disconnect upstream
  IntegrationTcpClientPtr tcp_client;
  FakeRawConnectionPtr fake_upstream_connection;
  const std::string upgrade_req_str = "GET /websocket/test HTTP/1.1\r\nHost: host\r\nConnection: "
                                      "Upgrade\r\nUpgrade: websocket\r\n\r\n";
  const std::string upgrade_resp_str =
      "HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n\r\n";
  executeActions(
      {[&]() -> void { tcp_client = makeTcpConnection(lookupPort("http")); },
       // Send websocket upgrade request
       [&]() -> void { tcp_client->write(upgrade_req_str); },
       [&]() -> void { fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection(); },
       [&]() -> void { fake_upstream_connection->waitForData(190); },
       // Accept websocket upgrade request
       [&]() -> void { fake_upstream_connection->write(upgrade_resp_str); },
       [&]() -> void { tcp_client->waitForData(upgrade_resp_str); },
       // Standard TCP proxy semantics post upgrade
       [&]() -> void { tcp_client->write("hello"); },
       [&]() -> void { fake_upstream_connection->waitForData(195); },
       [&]() -> void { fake_upstream_connection->write("world"); },
       // upstream disconnect
       [&]() -> void { fake_upstream_connection->close(); },
       [&]() -> void { fake_upstream_connection->waitForDisconnect(); },
       [&]() -> void { tcp_client->waitForDisconnect(); }});

  EXPECT_EQ(upgrade_resp_str + "world", tcp_client->data());
}

// TODO (rshriram): Since we are doing plain tcp proxying when we
// see upgrade headers from downstream, we are relying on the
// upstream to reject bad upgrade requests and close the
// connection. A better implementation would be one where we look at
// the upstream response headers and then switch the upstream and
// downstream to standard TCP proxy mode. Since this is a
// non-trivial change, a temporary solution could require the user
// to explicitly mark a route as websocket route.
TEST_P(IntegrationTest, WebSocketConnectionUpgradeRejected) {
  IntegrationTcpClientPtr tcp_client;
  FakeRawConnectionPtr fake_upstream_connection;
  const std::string upgrade_req_str =
      "GET /test/foo HTTP/1.1\r\nHost: host\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n\r\n";
  const std::string upgrade_resp_str = "HTTP/1.1 400 Bad Request\r\n\r\n";
  executeActions(
      {[&]() -> void { tcp_client = makeTcpConnection(lookupPort("http")); },
       // Send websocket upgrade request
       [&]() -> void { tcp_client->write(upgrade_req_str); },
       [&]() -> void { fake_upstream_connection = fake_upstreams_[1]->waitForRawConnection(); },
       [&]() -> void { fake_upstream_connection->waitForData(184); },
       // Reject websocket upgrade request
       [&]() -> void { fake_upstream_connection->write(upgrade_resp_str); },
       // upstream disconnect
       [&]() -> void { fake_upstream_connection->close(); },
       [&]() -> void { fake_upstream_connection->waitForDisconnect(); },
       [&]() -> void { tcp_client->waitForDisconnect(); }});

  EXPECT_EQ(upgrade_resp_str, tcp_client->data());
}

TEST_P(IntegrationTest, TcpProxyUpstreamDisconnect) {
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

TEST_P(IntegrationTest, TcpProxyDownstreamDisconnect) {
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
} // namespace Envoy
