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

static std::string websocketTestParamsToString(
    const testing::TestParamInfo<std::tuple<Network::Address::IpVersion, bool>> params) {
  return absl::StrCat(std::get<0>(params.param) == Network::Address::IpVersion::v4 ? "IPv4"
                                                                                   : "IPv6",
                      "_", std::get<1>(params.param) == true ? "OldStyle" : "NewStyle");
}

} // namespace

INSTANTIATE_TEST_CASE_P(IpVersions, WebsocketIntegrationTest,
                        testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                                         testing::Bool()),
                        websocketTestParamsToString);

ConfigHelper::HttpModifierFunction
setRouteUsingWebsocket(const envoy::api::v2::route::RouteAction::WebSocketProxyConfig* ws_config,
                       bool old_style) {
  if (!old_style) {
    return [](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager&
                  hcm) { hcm.add_upgrade_configs()->set_upgrade_type("websocket"); };
  }
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
  if (old_style_websockets_) {
    // Set a less permissive default route so it does not pick up the /websocket query.
    config_helper_.setDefaultHostAndRoute("*", "/asd");
  }
  HttpIntegrationTest::initialize();
}

void WebsocketIntegrationTest::validateInitialUpstreamData(const std::string& received_data) {
  if (old_style_websockets_) {
    // The request path gets rewritten from /websocket/test to /websocket.
    // The size of headers received by the destination is 228 bytes.
    EXPECT_EQ(received_data.size(), 228);
  }
  // In HTTP1, the transfer-length is defined by use of the "chunked" transfer-coding, even if
  // content-length header is present. No body websocket upgrade request send to upstream has
  // content-length header and has no transfer-encoding header.
  EXPECT_NE(received_data.find("content-length:"), std::string::npos);
  EXPECT_EQ(received_data.find("transfer-encoding:"), std::string::npos);
}

void WebsocketIntegrationTest::validateInitialDownstreamData(const std::string& received_data,
                                                             const std::string& expected_data) {
  if (old_style_websockets_) {
    ASSERT_EQ(expected_data, received_data);
  } else {
    // Strip out the date header since we're not going to generate an exact match.
    std::regex extra_request_headers("date:.*\r\nserver: envoy\r\n");
    std::string stripped_data = std::regex_replace(received_data, extra_request_headers, "");
    EXPECT_EQ(expected_data, stripped_data);
  }
}

void WebsocketIntegrationTest::validateFinalDownstreamData(const std::string& received_data,
                                                           const std::string& expected_data) {
  if (old_style_websockets_) {
    EXPECT_EQ(received_data, expected_data);
  } else {
    // Strip out the date header since we're not going to generate an exact match.
    std::regex extra_request_headers("date:.*\r\nserver: envoy\r\n");
    std::string stripped_data = std::regex_replace(received_data, extra_request_headers, "");
    EXPECT_EQ(expected_data, stripped_data);
  }
}

void WebsocketIntegrationTest::validateFinalUpstreamData(const std::string& received_data,
                                                         const std::string& expected_data) {
  std::regex extra_response_headers("x-request-id:.*\r\n");
  std::string stripped_data = std::regex_replace(received_data, extra_response_headers, "");
  EXPECT_EQ(expected_data, stripped_data);
}

TEST_P(WebsocketIntegrationTest, WebSocketConnectionDownstreamDisconnect) {
  config_helper_.addConfigModifier(setRouteUsingWebsocket(nullptr, old_style_websockets_));
  initialize();

  // WebSocket upgrade, send some data and disconnect downstream
  IntegrationTcpClientPtr tcp_client;
  FakeRawConnectionPtr fake_upstream_connection;

  tcp_client = makeTcpConnection(lookupPort("http"));
  // Send websocket upgrade request
  tcp_client->write(upgrade_req_str_);
  if (old_style_websockets_) {
    test_server_->waitForCounterGe("tcp.websocket.downstream_cx_total", 1);
  }
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(&headersRead, &data));
  validateInitialUpstreamData(data);

  // Accept websocket upgrade request
  ASSERT_TRUE(fake_upstream_connection->write(upgrade_resp_str_));
  tcp_client->waitForData("\r\n\r\n", false);
  validateInitialDownstreamData(tcp_client->data(), downstreamRespStr());

  // Standard TCP proxy semantics post upgrade
  tcp_client->write("hello");

  ASSERT_TRUE(
      fake_upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("hello")));
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  tcp_client->waitForData("world", false);
  tcp_client->write("bye!");

  // downstream disconnect
  tcp_client->close();
  std::string final_data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("bye"),
                                                    &final_data));
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  validateFinalDownstreamData(tcp_client->data(), downstreamRespStr() + "world");

  if (old_style_websockets_) {
    return;
  }

  const std::string upstream_payload = "GET /websocket/test HTTP/1.1\r\n"
                                       "host: host\r\n"
                                       "connection: keep-alive, Upgrade\r\n"
                                       "upgrade: websocket\r\n"
                                       "content-length: 0\r\n"
                                       "x-forwarded-proto: http\r\n"
                                       "x-envoy-expected-rq-timeout-ms: 15000\r\n\r\n"
                                       "hellobye!";
  validateFinalUpstreamData(final_data, upstream_payload);
}

TEST_P(WebsocketIntegrationTest, WebSocketConnectionUpstreamDisconnect) {
  config_helper_.addConfigModifier(setRouteUsingWebsocket(nullptr, old_style_websockets_));
  initialize();

  // WebSocket upgrade, send some data and disconnect upstream
  IntegrationTcpClientPtr tcp_client;
  FakeRawConnectionPtr fake_upstream_connection;
  tcp_client = makeTcpConnection(lookupPort("http"));
  // Send websocket upgrade request
  tcp_client->write(upgrade_req_str_);
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(&headersRead, &data));
  validateInitialUpstreamData(data);

  // Accept websocket upgrade request
  ASSERT_TRUE(fake_upstream_connection->write(upgrade_resp_str_));
  tcp_client->waitForData("\r\n\r\n", false);
  validateInitialDownstreamData(tcp_client->data(), downstreamRespStr());

  // Standard TCP proxy semantics post upgrade
  tcp_client->write("hello");

  ASSERT_TRUE(
      fake_upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("hello")));

  ASSERT_TRUE(fake_upstream_connection->write("world"));
  // upstream disconnect
  ASSERT_TRUE(fake_upstream_connection->close());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->waitForData("world", false);
  tcp_client->waitForDisconnect();
  ASSERT(!fake_upstream_connection->connected());

  validateFinalDownstreamData(tcp_client->data(), downstreamRespStr() + "world");
}

TEST_P(WebsocketIntegrationTest, EarlyData) {
  config_helper_.addConfigModifier(setRouteUsingWebsocket(nullptr, old_style_websockets_));
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
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Wait for both the upgrade, and the early data.
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch(early_data_req_str.c_str()), &data));
  // We expect to find the early data on the upstream side
  EXPECT_TRUE(StringUtil::endsWith(data, early_data_req_str));
  // Accept websocket upgrade request
  ASSERT_TRUE(fake_upstream_connection->write(upgrade_resp_str_));
  // Reply also with early data
  ASSERT_TRUE(fake_upstream_connection->write(early_data_resp_str));
  // upstream disconnect
  ASSERT_TRUE(fake_upstream_connection->close());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->waitForData(early_data_resp_str, false);
  tcp_client->waitForDisconnect();

  validateFinalDownstreamData(tcp_client->data(), downstreamRespStr() + "world");
}

TEST_P(WebsocketIntegrationTest, WebSocketConnectionIdleTimeout) {
  envoy::api::v2::route::RouteAction::WebSocketProxyConfig ws_config;
  ws_config.mutable_idle_timeout()->set_nanos(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(100)).count());
  *ws_config.mutable_stat_prefix() = "my-stat-prefix";
  config_helper_.addConfigModifier(setRouteUsingWebsocket(&ws_config, old_style_websockets_));
  if (!old_style_websockets_) {
    config_helper_.addConfigModifier(
        [&](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm)
            -> void {
          auto* route_config = hcm.mutable_route_config();
          auto* virtual_host = route_config->mutable_virtual_hosts(0);
          auto* route = virtual_host->mutable_routes(0)->mutable_route();
          route->mutable_idle_timeout()->set_seconds(0);
          route->mutable_idle_timeout()->set_nanos(200 * 1000 * 1000);
        });
  }
  initialize();

  // WebSocket upgrade, send some data and disconnect downstream
  IntegrationTcpClientPtr tcp_client;
  FakeRawConnectionPtr fake_upstream_connection;
  tcp_client = makeTcpConnection(lookupPort("http"));
  // Send websocket upgrade request
  // The request path gets rewritten from /websocket/test to /websocket.
  // The size of headers received by the destination is 228 bytes.
  tcp_client->write(upgrade_req_str_);
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(&headersRead, &data));
  validateInitialUpstreamData(data);

  // Accept websocket upgrade request
  ASSERT_TRUE(fake_upstream_connection->write(upgrade_resp_str_));
  tcp_client->waitForData("\r\n\r\n", false);
  validateInitialDownstreamData(tcp_client->data(), downstreamRespStr());
  // Standard TCP proxy semantics post upgrade
  tcp_client->write("hello");
  tcp_client->write("hello");
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  tcp_client->waitForData("world", false);

  if (old_style_websockets_) {
    test_server_->waitForCounterGe("tcp.my-stat-prefix.idle_timeout", 1);
  } else {
    test_server_->waitForCounterGe("http.config_test.downstream_rq_idle_timeout", 1);
  }
  tcp_client->waitForDisconnect();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
}

TEST_P(WebsocketIntegrationTest, WebSocketLogging) {
  if (!old_style_websockets_)
    return;
  envoy::api::v2::route::RouteAction::WebSocketProxyConfig ws_config;
  ws_config.mutable_idle_timeout()->set_nanos(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(100)).count());
  *ws_config.mutable_stat_prefix() = "my-stat-prefix";

  config_helper_.addConfigModifier(setRouteUsingWebsocket(&ws_config, old_style_websockets_));
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
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(228, &data));
  // Accept websocket upgrade request
  ASSERT_TRUE(fake_upstream_connection->write(upgrade_resp_str_));
  tcp_client->waitForData(upgrade_resp_str_);
  // Standard TCP proxy semantics post upgrade
  tcp_client->write("hello");
  // datalen = 228 + strlen(hello)
  ASSERT_TRUE(fake_upstream_connection->waitForData(233));
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  tcp_client->waitForData(upgrade_resp_str_ + "world");

  ASSERT_TRUE(fake_upstream_connection->close());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

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

// Technically not a websocket tests, but verfies normal upgrades have parity
// with websocket upgrades
TEST_P(WebsocketIntegrationTest, NonWebsocketUpgrade) {
  if (old_style_websockets_) {
    return;
  }
  config_helper_.addConfigModifier(
      [&](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm)
          -> void {
        auto* foo_upgrade = hcm.add_upgrade_configs();
        foo_upgrade->set_upgrade_type("foo");
      });

  config_helper_.addConfigModifier(setRouteUsingWebsocket(nullptr, old_style_websockets_));
  initialize();

  const std::string upgrade_req_str = "GET / HTTP/1.1\r\nHost: host\r\nConnection: "
                                      "keep-alive, Upgrade\r\nUpgrade: foo\r\n\r\n";
  const std::string upgrade_resp_str =
      "HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\nUpgrade: foo\r\n\r\n";

  // Upgrade, send some data and disconnect downstream
  IntegrationTcpClientPtr tcp_client;
  FakeRawConnectionPtr fake_upstream_connection;

  tcp_client = makeTcpConnection(lookupPort("http"));
  // Send websocket upgrade request
  // The size of headers received by the destination is 228 bytes.
  tcp_client->write(upgrade_req_str);
  if (old_style_websockets_) {
    test_server_->waitForCounterGe("tcp.websocket.downstream_cx_total", 1);
  }
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(&headersRead, &data));
  validateInitialUpstreamData(data);

  // Accept websocket upgrade request
  ASSERT_TRUE(fake_upstream_connection->write(upgrade_resp_str));
  tcp_client->waitForData("\r\n\r\n", false);
  if (old_style_websockets_) {
    ASSERT_EQ(tcp_client->data(), upgrade_resp_str);
  }
  // Standard TCP proxy semantics post upgrade
  tcp_client->write("hello");

  ASSERT_TRUE(
      fake_upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("hello")));
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  tcp_client->waitForData("world", false);
  tcp_client->write("bye!");

  // downstream disconnect
  tcp_client->close();
  std::string final_data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("bye"),
                                                    &final_data));
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  const std::string modified_upgrade_resp_str = "HTTP/1.1 101 Switching Protocols\r\nconnection: "
                                                "Upgrade\r\nupgrade: foo\r\ncontent-length: "
                                                "0\r\n\r\n";
  validateFinalDownstreamData(tcp_client->data(), modified_upgrade_resp_str + "world");
  const std::string upstream_payload = "GET / HTTP/1.1\r\n"
                                       "host: host\r\n"
                                       "connection: keep-alive, Upgrade\r\n"
                                       "upgrade: foo\r\n"
                                       "content-length: 0\r\n"
                                       "x-forwarded-proto: http\r\n"
                                       "x-envoy-expected-rq-timeout-ms: 15000\r\n\r\n"
                                       "hellobye!";

  std::regex extra_response_headers("x-request-id:.*\r\n");
  std::string stripped_data = std::regex_replace(final_data, extra_response_headers, "");
  EXPECT_EQ(upstream_payload, stripped_data);
}

TEST_P(WebsocketIntegrationTest, WebsocketCustomFilterChain) {
  config_helper_.addConfigModifier(setRouteUsingWebsocket(nullptr, old_style_websockets_));
  if (old_style_websockets_) {
    return;
  }

  // Add a small buffer filter to the standard HTTP filter chain. Websocket
  // upgrades will use the HTTP filter chain so will also have small buffers.
  config_helper_.addFilter(ConfigHelper::SMALL_BUFFER_FILTER);

  // Add a second upgrade type which goes directly to the router filter.
  config_helper_.addConfigModifier(
      [&](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm)
          -> void {
        auto* foo_upgrade = hcm.add_upgrade_configs();
        foo_upgrade->set_upgrade_type("foo");
        auto* filter_list_back = foo_upgrade->add_filters();
        const std::string json =
            Json::Factory::loadFromYamlString("name: envoy.router")->asJsonString();
        MessageUtil::loadFromJson(json, *filter_list_back);
      });
  initialize();

  // Websocket upgrades are configured to disallow large payload.
  const std::string early_data_req_str(2048, 'a');
  {
    const std::string upgrade_req_str =
        fmt::format("GET /websocket/test HTTP/1.1\r\nHost: host\r\nConnection: "
                    "keep-alive, Upgrade\r\nUpgrade: websocket\r\nContent-Length: {}\r\n\r\n",
                    early_data_req_str.length());
    IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));
    tcp_client->write(upgrade_req_str + early_data_req_str);
    tcp_client->waitForData("\r\n\r\n", false);
    EXPECT_NE(tcp_client->data().find("413"), std::string::npos);
    tcp_client->waitForDisconnect(true);
  }

  // HTTP requests are configured to disallow large bodies.
  {
    const std::string upgrade_req_str = fmt::format("GET / HTTP/1.1\r\nHost: host\r\nConnection: "
                                                    "keep-alive\r\nContent-Length: {}\r\n\r\n",
                                                    early_data_req_str.length());
    IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));
    tcp_client->write(upgrade_req_str + early_data_req_str);
    tcp_client->waitForData("\r\n\r\n", false);
    EXPECT_NE(tcp_client->data().find("413"), std::string::npos);
    tcp_client->waitForDisconnect(true);
  }

  // Foo upgrades are configured without the buffer filter, so should explicitly
  // allow large payload.
  {
    const std::string upgrade_req_str =
        fmt::format("GET /websocket/test HTTP/1.1\r\nHost: host\r\nConnection: "
                    "keep-alive, Upgrade\r\nUpgrade: foo\r\nContent-Length: {}\r\n\r\n",
                    early_data_req_str.length());
    IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));
    tcp_client->write(upgrade_req_str + early_data_req_str);
    FakeRawConnectionPtr fake_upstream_connection;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
    // Make sure the full payload arrives.
    ASSERT_TRUE(fake_upstream_connection->waitForData(
        FakeRawConnection::waitForInexactMatch(early_data_req_str.c_str())));
    // Tear down all the connections cleanly.
    tcp_client->close();
    ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  }
}

TEST_P(WebsocketIntegrationTest, BidirectionalChunkedData) {
  config_helper_.addConfigModifier(setRouteUsingWebsocket(nullptr, old_style_websockets_));
  initialize();
  const std::string upgrade_req_str = "GET /websocket/test HTTP/1.1\r\nHost: host\r\nconnection: "
                                      "keep-alive, Upgrade\r\nupgrade: Websocket\r\n"
                                      "transfer-encoding: chunked\r\n\r\n"
                                      "3\r\n123\r\n0\r\n\r\n"
                                      "SomeWebSocketPayload";

  // Upgrade, send initial data and wait for it to be received.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));
  tcp_client->write(upgrade_req_str);
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("SomeWebSocketPayload")));

  // Finish the upgrade.
  const std::string upgrade_resp_str =
      "HTTP/1.1 101 Switching Protocols\r\nconnection: Upgrade\r\nupgrade: Websocket\r\n"
      "transfer-encoding: chunked\r\n\r\n"
      "4\r\nabcd\r\n0\r\n\r\n"
      "SomeWebsocketResponsePayload";
  ASSERT_TRUE(fake_upstream_connection->write(upgrade_resp_str));
  tcp_client->waitForData("SomeWebsocketResponsePayload", false);

  // Verify bidirectional data still works.
  tcp_client->write("FinalClientPayload");
  std::string final_data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("FinalClientPayload"), &final_data));
  ASSERT_TRUE(fake_upstream_connection->write("FinalServerPayload"));
  tcp_client->waitForData("FinalServerPayload", false);

  // Clean up.
  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  const std::string modified_upstream_payload =
      "GET /websocket/test HTTP/1.1\r\n"
      "host: host\r\n"
      "connection: keep-alive, Upgrade\r\n"
      "upgrade: Websocket\r\n"
      "x-forwarded-proto: http\r\n"
      "x-envoy-expected-rq-timeout-ms: 15000\r\n"
      "transfer-encoding: chunked\r\n\r\n"
      "3\r\n123\r\n0\r\n\r\nSomeWebSocketPayloadFinalClientPayload";
  const std::string old_style_modified_payload =
      "GET /websocket HTTP/1.1\r\n"
      "host: host\r\n"
      "connection: keep-alive, Upgrade\r\n"
      "upgrade: Websocket\r\n"
      "x-forwarded-proto: http\r\n"
      "x-envoy-original-path: /websocket/test\r\n"
      "transfer-encoding: chunked\r\n\r\n"
      "3\r\n123\r\n0\r\n\r\nSomeWebSocketPayloadFinalClientPayload";
  validateFinalUpstreamData(final_data, old_style_websockets_ ? old_style_modified_payload
                                                              : modified_upstream_payload);

  const std::string modified_downstream_payload =
      "HTTP/1.1 101 Switching Protocols\r\nconnection: Upgrade\r\nupgrade: Websocket\r\n"
      "transfer-encoding: chunked\r\n\r\n"
      "4\r\nabcd\r\n0\r\n\r\n"
      "SomeWebsocketResponsePayloadFinalServerPayload";
  validateFinalDownstreamData(tcp_client->data(), modified_downstream_payload);
}

} // namespace Envoy
