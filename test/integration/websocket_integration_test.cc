#include "test/integration/websocket_integration_test.h"

#include <string>

#include "envoy/config/accesslog/v2/file.pb.h"

#include "common/filesystem/filesystem_impl.h"
#include "common/http/header_map_impl.h"
#include "common/protobuf/utility.h"

#include "test/integration/utility.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::MatchesRegex;

namespace Envoy {
namespace {

bool headersRead(const std::string& data) { return data.find("\r\n\r\n") != std::string::npos; }

} // namespace

INSTANTIATE_TEST_CASE_P(IpVersions, WebsocketIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

ConfigHelper::HttpModifierFunction
setRouteUsingWebsocket(const envoy::api::v2::route::RouteAction::WebSocketProxyConfig* ws_config) {
  return
      [ws_config](
          envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm) {
        auto route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->add_routes();
        route->mutable_match()->set_prefix("/websocket/test");
        route->mutable_route()->set_prefix_rewrite("/websocket");
        route->mutable_route()->set_cluster("cluster_0");
        route->mutable_route()->mutable_use_websocket()->set_value(true);

        if (ws_config != nullptr) {
          *route->mutable_route()->mutable_websocket_config() = *ws_config;
        }
      };
}

void WebsocketIntegrationTest::initialize() {
  // Set a less permissive default route so it does not pick up the /websocket query.
  config_helper_.setDefaultHostAndRoute("*", "/asd");
  HttpIntegrationTest::initialize();
}

void WebsocketIntegrationTest::validateInitialUpstreamData(const std::string& received_data) {
  // The request path gets rewritten from /websocket/test to /websocket.
  // The size of headers received by the destination is 228 bytes.
  EXPECT_EQ(received_data.size(), 228);
  // In HTTP1, the transfer-length is defined by use of the "chunked" transfer-coding, even if
  // content-length header is present. No body websocket upgrade request send to upstream has
  // content-length header and has no transfer-encoding header.
  EXPECT_NE(received_data.find("content-length:"), std::string::npos);
  EXPECT_EQ(received_data.find("transfer-encoding:"), std::string::npos);
}

void WebsocketIntegrationTest::validateInitialDownstreamData(const std::string& received_data) {
  ASSERT_EQ(received_data, upgrade_resp_str_);
}

void WebsocketIntegrationTest::validateFinalDownstreamData(const std::string& received_data,
                                                           const std::string& expected_data) {
  EXPECT_EQ(received_data, expected_data);
}

void WebsocketIntegrationTest::validateFinalUpstreamData(const std::string& received_data,
                                                         const std::string& expected_data) {
  std::regex extra_response_headers("x-request-id:.*\r\n");
  std::string stripped_data = std::regex_replace(received_data, extra_response_headers, "");
  EXPECT_EQ(expected_data, stripped_data);
}

TEST_P(WebsocketIntegrationTest, WebSocketConnectionDownstreamDisconnect) {
  config_helper_.addConfigModifier(setRouteUsingWebsocket(nullptr));
  initialize();

  // WebSocket upgrade, send some data and disconnect downstream
  IntegrationTcpClientPtr tcp_client;
  FakeRawConnectionPtr fake_upstream_connection;

  tcp_client = makeTcpConnection(lookupPort("http"));
  // Send websocket upgrade request
  tcp_client->write(upgrade_req_str_);
  test_server_->waitForCounterGe("tcp.websocket.downstream_cx_total", 1);
  fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();
  const std::string data = fake_upstream_connection->waitForData(&headersRead);
  validateInitialUpstreamData(data);

  // Accept websocket upgrade request
  fake_upstream_connection->write(upgrade_resp_str_);
  tcp_client->waitForData("\r\n\r\n", false);
  validateInitialDownstreamData(tcp_client->data());

  // Standard TCP proxy semantics post upgrade
  tcp_client->write("hello");

  fake_upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("hello"));
  fake_upstream_connection->write("world");
  tcp_client->waitForData("world", false);
  tcp_client->write("bye!");

  // downstream disconnect
  tcp_client->close();
  fake_upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("bye"));
  fake_upstream_connection->waitForDisconnect();

  validateFinalDownstreamData(tcp_client->data(), upgrade_resp_str_ + "world");
}

TEST_P(WebsocketIntegrationTest, WebSocketConnectionUpstreamDisconnect) {
  config_helper_.addConfigModifier(setRouteUsingWebsocket(nullptr));
  initialize();

  // WebSocket upgrade, send some data and disconnect upstream
  IntegrationTcpClientPtr tcp_client;
  FakeRawConnectionPtr fake_upstream_connection;
  tcp_client = makeTcpConnection(lookupPort("http"));
  // Send websocket upgrade request
  tcp_client->write(upgrade_req_str_);
  fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();
  ASSERT_TRUE(fake_upstream_connection != nullptr);
  const std::string data = fake_upstream_connection->waitForData(&headersRead);
  validateInitialUpstreamData(data);

  // Accept websocket upgrade request
  fake_upstream_connection->write(upgrade_resp_str_);
  tcp_client->waitForData("\r\n\r\n", false);
  validateInitialDownstreamData(tcp_client->data());

  // Standard TCP proxy semantics post upgrade
  tcp_client->write("hello");

  fake_upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("hello"));

  fake_upstream_connection->write("world");
  // upstream disconnect
  fake_upstream_connection->close();
  fake_upstream_connection->waitForDisconnect();
  tcp_client->waitForData("world", false);
  tcp_client->waitForDisconnect();
  ASSERT(!fake_upstream_connection->connected());

  validateFinalDownstreamData(tcp_client->data(), upgrade_resp_str_ + "world");
}

TEST_P(WebsocketIntegrationTest, EarlyData) {
  config_helper_.addConfigModifier(setRouteUsingWebsocket(nullptr));
  initialize();

  // WebSocket upgrade with early data (HTTP body)
  IntegrationTcpClientPtr tcp_client;
  FakeRawConnectionPtr fake_upstream_connection;
  const std::string early_data_req_str = "hello";
  const std::string early_data_resp_str = "world";
  const std::string upgrade_req_str =
      fmt::format("GET /websocket/test HTTP/1.1\r\nHost: host\r\nConnection: "
                  "keep-alive, Upgrade\r\nUpgrade: websocket\r\nContent-Length: {}\r\n\r\n",
                  early_data_req_str.length());
  tcp_client = makeTcpConnection(lookupPort("http"));
  // Send early data alongside websocket upgrade request
  tcp_client->write(upgrade_req_str + early_data_req_str);
  fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();

  // Wait for both the upgrade, and the early data.
  const std::string data = fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch(early_data_req_str.c_str()));
  // We expect to find the early data on the upstream side
  EXPECT_TRUE(StringUtil::endsWith(data, early_data_req_str));
  // Accept websocket upgrade request
  fake_upstream_connection->write(upgrade_resp_str_);
  // Reply also with early data
  fake_upstream_connection->write(early_data_resp_str);
  // upstream disconnect
  fake_upstream_connection->close();
  fake_upstream_connection->waitForDisconnect();
  tcp_client->waitForData(early_data_resp_str, false);
  tcp_client->waitForDisconnect();

  validateFinalDownstreamData(tcp_client->data(), upgrade_resp_str_ + "world");
}

TEST_P(WebsocketIntegrationTest, WebSocketConnectionIdleTimeout) {
  envoy::api::v2::route::RouteAction::WebSocketProxyConfig ws_config;
  ws_config.mutable_idle_timeout()->set_nanos(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(100)).count());
  *ws_config.mutable_stat_prefix() = "my-stat-prefix";
  config_helper_.addConfigModifier(setRouteUsingWebsocket(&ws_config));
  initialize();

  // WebSocket upgrade, send some data and disconnect downstream
  IntegrationTcpClientPtr tcp_client;
  FakeRawConnectionPtr fake_upstream_connection;
  tcp_client = makeTcpConnection(lookupPort("http"));
  // Send websocket upgrade request
  // The request path gets rewritten from /websocket/test to /websocket.
  // The size of headers received by the destination is 228 bytes.
  tcp_client->write(upgrade_req_str_);
  fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();
  const std::string data = fake_upstream_connection->waitForData(&headersRead);
  validateInitialUpstreamData(data);

  // Accept websocket upgrade request
  fake_upstream_connection->write(upgrade_resp_str_);
  tcp_client->waitForData("\r\n\r\n", false);
  validateInitialDownstreamData(tcp_client->data());
  // Standard TCP proxy semantics post upgrade
  tcp_client->write("hello");
  tcp_client->write("hello");
  fake_upstream_connection->write("world");
  tcp_client->waitForData("world", false);

  test_server_->waitForCounterGe("tcp.my-stat-prefix.idle_timeout", 1);
  tcp_client->waitForDisconnect();
  fake_upstream_connection->waitForDisconnect();
}

TEST_P(WebsocketIntegrationTest, WebSocketLogging) {
  envoy::api::v2::route::RouteAction::WebSocketProxyConfig ws_config;
  ws_config.mutable_idle_timeout()->set_nanos(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(100)).count());
  *ws_config.mutable_stat_prefix() = "my-stat-prefix";

  config_helper_.addConfigModifier(setRouteUsingWebsocket(&ws_config));

  std::string expected_log_template = "bytes_sent={0} "
                                      "bytes_received={1} "
                                      "downstream_local_address={2} "
                                      "downstream_remote_address={3} "
                                      "upstream_local_address={4}";

  std::string access_log_path = TestEnvironment::temporaryPath(fmt::format(
      "websocket_access_log{}.txt", version_ == Network::Address::IpVersion::v4 ? "v4" : "v6"));
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* config_blob = filter_chain->mutable_filters(0)->mutable_config();

    envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager
        http_conn_manager_config;
    MessageUtil::jsonConvert(*config_blob, http_conn_manager_config);

    auto* access_log = http_conn_manager_config.add_access_log();
    access_log->set_name("envoy.file_access_log");
    envoy::config::accesslog::v2::FileAccessLog access_log_config;
    access_log_config.set_path(access_log_path);
    access_log_config.set_format(fmt::format(
        expected_log_template, "%BYTES_SENT%", "%BYTES_RECEIVED%", "%DOWNSTREAM_LOCAL_ADDRESS%",
        "%DOWNSTREAM_REMOTE_ADDRESS%", "%UPSTREAM_LOCAL_ADDRESS%"));

    MessageUtil::jsonConvert(access_log_config, *access_log->mutable_config());
    MessageUtil::jsonConvert(http_conn_manager_config, *config_blob);
  });

  initialize();

  // WebSocket upgrade, send some data and disconnect downstream
  IntegrationTcpClientPtr tcp_client;
  FakeRawConnectionPtr fake_upstream_connection;

  tcp_client = makeTcpConnection(lookupPort("http"));
  // Send websocket upgrade request
  // The request path gets rewritten from /websocket/test to /websocket.
  // The size of headers received by the destination is 228 bytes.
  tcp_client->write(upgrade_req_str_);
  fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();
  const std::string data = fake_upstream_connection->waitForData(228);
  // Accept websocket upgrade request
  fake_upstream_connection->write(upgrade_resp_str_);
  tcp_client->waitForData(upgrade_resp_str_);
  // Standard TCP proxy semantics post upgrade
  tcp_client->write("hello");
  // datalen = 228 + strlen(hello)
  fake_upstream_connection->waitForData(233);
  fake_upstream_connection->write("world");
  tcp_client->waitForData(upgrade_resp_str_ + "world");

  fake_upstream_connection->close();
  fake_upstream_connection->waitForDisconnect();

  tcp_client->waitForDisconnect();
  tcp_client->close();

  std::string log_result;
  do {
    log_result = Filesystem::fileReadToEnd(access_log_path);
  } while (log_result.empty());

  const std::string ip_port_regex = (version_ == Network::Address::IpVersion::v4)
                                        ? R"EOF(127\.0\.0\.1:[0-9]+)EOF"
                                        : R"EOF(\[::1\]:[0-9]+)EOF";

  EXPECT_THAT(log_result, MatchesRegex(fmt::format(expected_log_template,
                                                   82, // response length
                                                   5,  // hello length
                                                   ip_port_regex, ip_port_regex, ip_port_regex)));
}

TEST_P(WebsocketIntegrationTest, BidirectionalChunkedData) {
  config_helper_.addConfigModifier(setRouteUsingWebsocket(nullptr));
  initialize();
  const std::string upgrade_req_str = "GET /websocket/test HTTP/1.1\r\nHost: host\r\nconnection: "
                                      "keep-alive, Upgrade\r\nupgrade: Websocket\r\n"
                                      "transfer-encoding: chunked\r\n\r\n"
                                      "3\r\n123\r\n0\r\n\r\n"
                                      "SomeWebSocketPayload";

  // Upgrade, send initial data and wait for it to be received.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));
  tcp_client->write(upgrade_req_str);
  FakeRawConnectionPtr fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();
  // TODO(alyssawilk) We should be able to wait for SomeWebSocketPayload but it
  // is not flushed immediately.
  const std::string data =
      fake_upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("\r\n\r\n"));

  // Finish the upgrade.
  const std::string upgrade_resp_str =
      "HTTP/1.1 101 Switching Protocols\r\nconnection: Upgrade\r\nupgrade: Websocket\r\n"
      "transfer-encoding: chunked\r\n\r\n"
      "4\r\nabcd\r\n0\r\n"
      "SomeWebsocketResponsePayload";
  fake_upstream_connection->write(upgrade_resp_str);
  tcp_client->waitForData("Payload", false);

  // Verify bidirectional data still works.
  tcp_client->write("FinalClientPayload");
  std::string final_data = fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("FinalClientPayload"));
  fake_upstream_connection->write("FinalServerPayload");
  tcp_client->waitForData("FinalServerPayload", false);

  // Clean up.
  tcp_client->close();
  fake_upstream_connection->waitForDisconnect();

  // TODO(alyssawilk) the current stack is stripping chunked encoding, then
  // adding back the chunked encoding header without actually chunk encoding.
  // Data about HTTP vs websocket data boundaries is therefore lost. Fix by
  // actually chunk encoding.
  const std::string old_style_modified_payload = "GET /websocket HTTP/1.1\r\n"
                                                 "host: host\r\n"
                                                 "connection: keep-alive, Upgrade\r\n"
                                                 "upgrade: Websocket\r\n"
                                                 "x-forwarded-proto: http\r\n"
                                                 "x-envoy-original-path: /websocket/test\r\n"
                                                 "transfer-encoding: chunked\r\n\r\n"
                                                 "123SomeWebSocketPayloadFinalClientPayload";
  validateFinalUpstreamData(final_data, old_style_modified_payload);

  const std::string modified_downstream_payload =
      "HTTP/1.1 101 Switching Protocols\r\nconnection: Upgrade\r\nupgrade: Websocket\r\n"
      "transfer-encoding: chunked\r\n\r\n"
      "4\r\nabcd\r\n0\r\n"
      "SomeWebsocketResponsePayloadFinalServerPayload";
  validateFinalDownstreamData(tcp_client->data(), modified_downstream_payload);
}

} // namespace Envoy
