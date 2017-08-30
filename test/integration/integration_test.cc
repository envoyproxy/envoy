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
  executeActions(
      {[&]() -> void {
         codec_client_ = makeHttpConnection(lookupPort("http"), Http::CodecClient::Type::HTTP1);
       },
       [&]() -> void {
         codec_client_->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                      {":path", "/healthcheck"},
                                                                      {":authority", "host"},
                                                                      {"connection", "close"}},
                                              *response_);
       },
       [&]() -> void { response_->waitForEndStream(); },
       [&]() -> void { codec_client_->waitForDisconnect(); }});

  EXPECT_TRUE(response_->complete());
  EXPECT_STREQ("200", response_->headers().Status()->value().c_str());
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
                                         Http::CodecClient::Type::HTTP1, true);
}

TEST_P(IntegrationTest, RouterHeaderOnlyRequestAndResponseBuffer) {
  testRouterHeaderOnlyRequestAndResponse(makeClientConnection(lookupPort("http_buffer")),
                                         Http::CodecClient::Type::HTTP1, true);
}

TEST_P(IntegrationTest, ShutdownWithActiveConnPoolConnections) {
  testRouterHeaderOnlyRequestAndResponse(makeClientConnection(lookupPort("http")),
                                         Http::CodecClient::Type::HTTP1, false);
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

TEST_P(IntegrationTest, AbsolutePath) { testAbsolutePath(); }

TEST_P(IntegrationTest, AbsolutePathWithPort) { testAbsolutePathWithPort(); }

TEST_P(IntegrationTest, AbsolutePathWithoutPort) { testAbsolutePathWithoutPort(); }

TEST_P(IntegrationTest, Connect) { testConnect(); }

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
      // The request path gets rewritten from /websocket/test to /websocket.
      // The size of headers received by the destination is 225 bytes.
      [&]() -> void { tcp_client->write(upgrade_req_str); },
      [&]() -> void { fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection(); },
      [&]() -> void { fake_upstream_connection->waitForData(225); },
      // Accept websocket upgrade request
      [&]() -> void { fake_upstream_connection->write(upgrade_resp_str); },
      [&]() -> void { tcp_client->waitForData(upgrade_resp_str); },
      // Standard TCP proxy semantics post upgrade
      [&]() -> void { tcp_client->write("hello"); },
      // datalen = 225 + strlen(hello)
      [&]() -> void { fake_upstream_connection->waitForData(230); },
      [&]() -> void { fake_upstream_connection->write("world"); },
      [&]() -> void { tcp_client->waitForData(upgrade_resp_str + "world"); },
      [&]() -> void { tcp_client->write("bye!"); },
      // downstream disconnect
      [&]() -> void { tcp_client->close(); },
      // datalen = 225 + strlen(hello) + strlen(bye!)
      [&]() -> void { fake_upstream_connection->waitForData(234); },
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
       // The request path gets rewritten from /websocket/test to /websocket.
       // The size of headers received by the destination is 225 bytes.
       [&]() -> void { fake_upstream_connection->waitForData(225); },
       // Accept websocket upgrade request
       [&]() -> void { fake_upstream_connection->write(upgrade_resp_str); },
       [&]() -> void { tcp_client->waitForData(upgrade_resp_str); },
       // Standard TCP proxy semantics post upgrade
       [&]() -> void { tcp_client->write("hello"); },
       // datalen = 225 + strlen(hello)
       [&]() -> void { fake_upstream_connection->waitForData(230); },
       [&]() -> void { fake_upstream_connection->write("world"); },
       // upstream disconnect
       [&]() -> void { fake_upstream_connection->close(); },
       [&]() -> void { fake_upstream_connection->waitForDisconnect(); },
       [&]() -> void { tcp_client->waitForDisconnect(); }});

  EXPECT_EQ(upgrade_resp_str + "world", tcp_client->data());
}

class BindIntegrationTest : public IntegrationTest {
public:
  void SetUp() override {
    envoy::api::v2::Bootstrap bootstrap;
    if (GetParam() == Network::Address::IpVersion::v4) {
      address_string_ = TestUtility::getIpv4Loopback();
    }
    bootstrap.mutable_cluster_manager()
        ->mutable_upstream_bind_config()
        ->mutable_source_address()
        ->set_address(address_string_);

    api_filesystem_config_.bootstrap_path_ =
        TestEnvironment::writeStringToFileForTest("bootstrap.pb", bootstrap.SerializeAsString());
    IntegrationTest::SetUp();
  }

  std::string address_string_ = "::1";
};

TEST_P(BindIntegrationTest, TestBind) {
  executeActions(
      {[&]() -> void {
         codec_client_ = makeHttpConnection(lookupPort("http"), Http::CodecClient::Type::HTTP1);
       },
       // Request 1.
       [&]() -> void {
         codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                    {":path", "/test/long/url"},
                                                                    {":scheme", "http"},
                                                                    {":authority", "host"}},
                                            1024, *response_);
       },
       [&]() -> void {
         fake_upstream_connection_ = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
         std::string address =
             fake_upstream_connection_->connection().remoteAddress().ip()->addressAsString();
         EXPECT_EQ(address, address_string_);
       },
       [&]() -> void { upstream_request_ = fake_upstream_connection_->waitForNewStream(); },
       [&]() -> void { upstream_request_->waitForEndStream(*dispatcher_); },
       // Cleanup both downstream and upstream
       [&]() -> void { codec_client_->close(); },
       [&]() -> void { fake_upstream_connection_->close(); },
       [&]() -> void { fake_upstream_connection_->waitForDisconnect(); }});
}

INSTANTIATE_TEST_CASE_P(IpVersions, BindIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));
} // namespace Envoy
