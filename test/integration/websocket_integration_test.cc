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

INSTANTIATE_TEST_CASE_P(IpVersions, WebsocketIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

ConfigHelper::HttpModifierFunction setRouteUsingWebsocket(
    const envoy::api::v2::route::RouteAction::WebSocketProxyConfig* ws_config = nullptr) {
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

TEST_P(WebsocketIntegrationTest, WebSocketConnectionDownstreamDisconnect) {
  // Set a less permissive default route so it does not pick up the /websocket query.
  config_helper_.setDefaultHostAndRoute("*", "/asd");
  // Enable websockets for the path /websocket/test
  config_helper_.addConfigModifier(setRouteUsingWebsocket());
  initialize();

  // WebSocket upgrade, send some data and disconnect downstream
  IntegrationTcpClientPtr tcp_client;
  FakeRawConnectionPtr fake_upstream_connection;
  const std::string upgrade_req_str = "GET /websocket/test HTTP/1.1\r\nHost: host\r\nConnection: "
                                      "keep-alive, Upgrade\r\nUpgrade: websocket\r\n\r\n";
  const std::string upgrade_resp_str =
      "HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n\r\n";

  tcp_client = makeTcpConnection(lookupPort("http"));
  // Send websocket upgrade request
  // The request path gets rewritten from /websocket/test to /websocket.
  // The size of headers received by the destination is 228 bytes.
  tcp_client->write(upgrade_req_str);
  test_server_->waitForCounterGe("tcp.websocket.downstream_cx_total", 1);
  fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();
  const std::string data = fake_upstream_connection->waitForData(228);
  // In HTTP1, the transfer-length is defined by use of the "chunked" transfer-coding, even if
  // content-length header is present. No body websocket upgrade request send to upstream has
  // content-length header and has no transfer-encoding header.
  EXPECT_NE(data.find("content-length:"), std::string::npos);
  EXPECT_EQ(data.find("transfer-encoding:"), std::string::npos);
  // Accept websocket upgrade request
  fake_upstream_connection->write(upgrade_resp_str);
  tcp_client->waitForData(upgrade_resp_str);
  // Standard TCP proxy semantics post upgrade
  tcp_client->write("hello");
  // datalen = 228 + strlen(hello)
  fake_upstream_connection->waitForData(233);
  fake_upstream_connection->write("world");
  tcp_client->waitForData(upgrade_resp_str + "world");
  tcp_client->write("bye!");
  // downstream disconnect
  tcp_client->close();
  // datalen = 228 + strlen(hello) + strlen(bye!)
  fake_upstream_connection->waitForData(237);
  fake_upstream_connection->waitForDisconnect();
}

TEST_P(WebsocketIntegrationTest, WebSocketConnectionUpstreamDisconnect) {
  config_helper_.setDefaultHostAndRoute("*", "/asd");
  config_helper_.addConfigModifier(setRouteUsingWebsocket());
  initialize();

  // WebSocket upgrade, send some data and disconnect upstream
  IntegrationTcpClientPtr tcp_client;
  FakeRawConnectionPtr fake_upstream_connection;
  const std::string upgrade_req_str = "GET /websocket/test HTTP/1.1\r\nHost: host\r\nConnection: "
                                      "keep-alive, Upgrade\r\nUpgrade: websocket\r\n\r\n";
  const std::string upgrade_resp_str =
      "HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n\r\n";
  tcp_client = makeTcpConnection(lookupPort("http"));
  // Send websocket upgrade request
  tcp_client->write(upgrade_req_str);
  fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();
  // The request path gets rewritten from /websocket/test to /websocket.
  // The size of headers received by the destination is 228 bytes.
  const std::string data = fake_upstream_connection->waitForData(228);
  // In HTTP1, the transfer-length is defined by use of the "chunked" transfer-coding, even if
  // content-length header is present. No body websocket upgrade request send to upstream has
  // content-length header and has no transfer-encoding header.
  EXPECT_NE(data.find("content-length:"), std::string::npos);
  EXPECT_EQ(data.find("transfer-encoding:"), std::string::npos);
  // Accept websocket upgrade request
  fake_upstream_connection->write(upgrade_resp_str);
  tcp_client->waitForData(upgrade_resp_str);
  // Standard TCP proxy semantics post upgrade
  tcp_client->write("hello");
  // datalen = 228 + strlen(hello)
  fake_upstream_connection->waitForData(233);
  fake_upstream_connection->write("world");
  // upstream disconnect
  fake_upstream_connection->close();
  fake_upstream_connection->waitForDisconnect();
  tcp_client->waitForDisconnect();

  EXPECT_EQ(upgrade_resp_str + "world", tcp_client->data());
}

TEST_P(WebsocketIntegrationTest, WebSocketConnectionEarlyData) {
  config_helper_.setDefaultHostAndRoute("*", "/asd");
  config_helper_.addConfigModifier(setRouteUsingWebsocket());
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
  const std::string upgrade_resp_str =
      "HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n\r\n";
  tcp_client = makeTcpConnection(lookupPort("http"));
  // Send early data alongside websocket upgrade request
  tcp_client->write(upgrade_req_str + early_data_req_str);
  fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();
  // The request path gets rewritten from /websocket/test to /websocket.
  // The size of headers received by the destination is 228 bytes
  // and we add the early data to that.
  const std::string data = fake_upstream_connection->waitForData(228 + early_data_req_str.length());
  // We expect to find the early data on the upstream side
  EXPECT_TRUE(StringUtil::endsWith(data, early_data_req_str));
  // Accept websocket upgrade request
  fake_upstream_connection->write(upgrade_resp_str);
  // Reply also with early data
  fake_upstream_connection->write(early_data_resp_str);
  // upstream disconnect
  fake_upstream_connection->close();
  fake_upstream_connection->waitForDisconnect();
  tcp_client->waitForDisconnect();

  EXPECT_EQ(upgrade_resp_str + early_data_resp_str, tcp_client->data());
}

TEST_P(WebsocketIntegrationTest, WebSocketIdleTimeout) {
  envoy::api::v2::route::RouteAction::WebSocketProxyConfig ws_config;
  ws_config.mutable_idle_timeout()->set_nanos(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(100)).count());
  *ws_config.mutable_stat_prefix() = "my-stat-prefix";
  config_helper_.setDefaultHostAndRoute("*", "/asd");
  config_helper_.addConfigModifier(setRouteUsingWebsocket(&ws_config));
  initialize();

  // WebSocket upgrade, send some data and disconnect downstream
  IntegrationTcpClientPtr tcp_client;
  FakeRawConnectionPtr fake_upstream_connection;
  const std::string upgrade_req_str = "GET /websocket/test HTTP/1.1\r\nHost: host\r\nConnection: "
                                      "keep-alive, Upgrade\r\nUpgrade: websocket\r\n\r\n";
  const std::string upgrade_resp_str =
      "HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n\r\n";

  tcp_client = makeTcpConnection(lookupPort("http"));
  // Send websocket upgrade request
  // The request path gets rewritten from /websocket/test to /websocket.
  // The size of headers received by the destination is 228 bytes.
  tcp_client->write(upgrade_req_str);
  fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();
  const std::string data = fake_upstream_connection->waitForData(228);
  // Accept websocket upgrade request
  fake_upstream_connection->write(upgrade_resp_str);
  tcp_client->waitForData(upgrade_resp_str);
  // Standard TCP proxy semantics post upgrade
  tcp_client->write("hello");
  // datalen = 228 + strlen(hello)
  fake_upstream_connection->waitForData(233);
  fake_upstream_connection->write("world");
  tcp_client->waitForData(upgrade_resp_str + "world");

  test_server_->waitForCounterGe("tcp.my-stat-prefix.idle_timeout", 1);
  tcp_client->waitForDisconnect();
  fake_upstream_connection->waitForDisconnect();
}

TEST_P(WebsocketIntegrationTest, WebSocketLogging) {
  envoy::api::v2::route::RouteAction::WebSocketProxyConfig ws_config;
  ws_config.mutable_idle_timeout()->set_nanos(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(100)).count());
  *ws_config.mutable_stat_prefix() = "my-stat-prefix";

  config_helper_.setDefaultHostAndRoute("*", "/asd");
  config_helper_.addConfigModifier(setRouteUsingWebsocket(&ws_config));

  std::string expected_log_template = "bytes_sent={0} "
                                      "bytes_received={1} "
                                      "downstream_local_address={2} "
                                      "downstream_remote_address={3} "
                                      "upstream_local_address={4}";

  std::string access_log_path = TestEnvironment::temporaryPath(fmt::format(
      "websocket_access_log{}.txt", GetParam() == Network::Address::IpVersion::v4 ? "v4" : "v6"));
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
  const std::string upgrade_req_str = "GET /websocket/test HTTP/1.1\r\nHost: host\r\nConnection: "
                                      "keep-alive, Upgrade\r\nUpgrade: websocket\r\n\r\n";
  const std::string upgrade_resp_str =
      "HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n\r\n";

  tcp_client = makeTcpConnection(lookupPort("http"));
  // Send websocket upgrade request
  // The request path gets rewritten from /websocket/test to /websocket.
  // The size of headers received by the destination is 228 bytes.
  tcp_client->write(upgrade_req_str);
  fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();
  const std::string data = fake_upstream_connection->waitForData(228);
  // Accept websocket upgrade request
  fake_upstream_connection->write(upgrade_resp_str);
  tcp_client->waitForData(upgrade_resp_str);
  // Standard TCP proxy semantics post upgrade
  tcp_client->write("hello");
  // datalen = 228 + strlen(hello)
  fake_upstream_connection->waitForData(233);
  fake_upstream_connection->write("world");
  tcp_client->waitForData(upgrade_resp_str + "world");

  fake_upstream_connection->close();
  fake_upstream_connection->waitForDisconnect();

  tcp_client->waitForDisconnect();
  tcp_client->close();

  std::string log_result;
  do {
    log_result = Filesystem::fileReadToEnd(access_log_path);
  } while (log_result.empty());

  const std::string ip_port_regex = (GetParam() == Network::Address::IpVersion::v4)
                                        ? R"EOF(127\.0\.0\.1:[0-9]+)EOF"
                                        : R"EOF(\[::1\]:[0-9]+)EOF";

  EXPECT_THAT(log_result, MatchesRegex(fmt::format(expected_log_template,
                                                   82, // response length
                                                   5,  // hello length
                                                   ip_port_regex, ip_port_regex, ip_port_regex)));
}

} // namespace Envoy
