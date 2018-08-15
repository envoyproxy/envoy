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

#include "absl/strings/str_cat.h"
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

void WebsocketIntegrationTest::performWebSocketUpgrade(const std::string& upgrade_req_string,
                                                       const std::string& upgrade_resp_string) {
  // Establish the initial connection.
  tcp_client_ = makeTcpConnection(lookupPort("http"));

  // Send the websocket upgrade request.
  tcp_client_->write(upgrade_req_string);
  if (old_style_websockets_) {
    test_server_->waitForCounterGe("tcp.websocket.downstream_cx_total", 1);
  }

  // Verify the upgrade was received upstream.
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_tcp_upstream_connection_));
  std::string data;
  ASSERT_TRUE(fake_tcp_upstream_connection_->waitForData(&headersRead, &data));
  validateInitialUpstreamData(data);

  // Send the upgrade response
  ASSERT_TRUE(fake_tcp_upstream_connection_->write(upgrade_resp_string));

  // Verify the upgrade response was received downstream.
  tcp_client_->waitForData("\r\n\r\n", false);
  validateInitialDownstreamData(tcp_client_->data(), downstreamRespStr());
}

void WebsocketIntegrationTest::sendBidirectionalData() {
  // Verify that the client can still send data upstream, and that upstream
  // receives it.
  tcp_client_->write("hello");
  ASSERT_TRUE(
      fake_tcp_upstream_connection_->waitForData(FakeRawConnection::waitForInexactMatch("hello")));

  // Verify the upstream can send data to the client and that the client
  // receives it.
  ASSERT_TRUE(fake_tcp_upstream_connection_->write("world"));
  tcp_client_->waitForData("world", false);
}

TEST_P(WebsocketIntegrationTest, WebSocketConnectionDownstreamDisconnect) {
  config_helper_.addConfigModifier(setRouteUsingWebsocket(nullptr, old_style_websockets_));
  initialize();

  performWebSocketUpgrade(upgrade_req_str_, upgrade_resp_str_);
  sendBidirectionalData();

  // Send some final data from the client, and disconnect.
  tcp_client_->write("bye!");
  tcp_client_->close();

  // Verify the final data was received and that the connection is torn down.
  std::string final_data;
  ASSERT_TRUE(fake_tcp_upstream_connection_->waitForData(
      FakeRawConnection::waitForInexactMatch("bye"), &final_data));
  ASSERT_TRUE(fake_tcp_upstream_connection_->waitForDisconnect());

  validateFinalDownstreamData(tcp_client_->data(), downstreamRespStr() + "world");

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

  performWebSocketUpgrade(upgrade_req_str_, upgrade_resp_str_);

  // Standard TCP proxy semantics post upgrade
  tcp_client_->write("hello");
  ASSERT_TRUE(
      fake_tcp_upstream_connection_->waitForData(FakeRawConnection::waitForInexactMatch("hello")));

  // Send data downstream and disconnect immediately.
  ASSERT_TRUE(fake_tcp_upstream_connection_->write("world"));
  ASSERT_TRUE(fake_tcp_upstream_connection_->close());
  ASSERT_TRUE(fake_tcp_upstream_connection_->waitForDisconnect());

  // Verify both the data and the disconnect went through.
  tcp_client_->waitForData("world", false);
  tcp_client_->waitForDisconnect();
  ASSERT(!fake_tcp_upstream_connection_->connected());

  validateFinalDownstreamData(tcp_client_->data(), downstreamRespStr() + "world");
}

TEST_P(WebsocketIntegrationTest, EarlyData) {
  config_helper_.addConfigModifier(setRouteUsingWebsocket(nullptr, old_style_websockets_));
  initialize();

  // WebSocket upgrade with early data (HTTP body)
  const std::string early_data_req_str = "hello";
  const std::string early_data_resp_str = "world";
  const std::string upgrade_req_str =
      createUpgradeRequest("websocket", early_data_req_str.length());
  tcp_client_ = makeTcpConnection(lookupPort("http"));
  // Send early data alongside websocket upgrade request
  tcp_client_->write(upgrade_req_str + early_data_req_str);
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_tcp_upstream_connection_));

  // Wait for both the upgrade, and the early data.
  std::string data;
  ASSERT_TRUE(fake_tcp_upstream_connection_->waitForData(
      FakeRawConnection::waitForInexactMatch(early_data_req_str.c_str()), &data));
  // We expect to find the early data on the upstream side
  EXPECT_TRUE(StringUtil::endsWith(data, early_data_req_str));
  // Accept websocket upgrade request
  ASSERT_TRUE(fake_tcp_upstream_connection_->write(upgrade_resp_str_));
  // Reply also with early data
  ASSERT_TRUE(fake_tcp_upstream_connection_->write(early_data_resp_str));
  // upstream disconnect
  ASSERT_TRUE(fake_tcp_upstream_connection_->close());
  ASSERT_TRUE(fake_tcp_upstream_connection_->waitForDisconnect());
  tcp_client_->waitForData(early_data_resp_str, false);
  tcp_client_->waitForDisconnect();

  validateFinalDownstreamData(tcp_client_->data(), downstreamRespStr() + "world");
}

TEST_P(WebsocketIntegrationTest, WebSocketConnectionIdleTimeout) {
  envoy::api::v2::route::RouteAction::WebSocketProxyConfig ws_config;
  ws_config.mutable_idle_timeout()->set_nanos(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(100)).count());
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
  performWebSocketUpgrade(upgrade_req_str_, upgrade_resp_str_);
  sendBidirectionalData();

  if (old_style_websockets_) {
    test_server_->waitForCounterGe("tcp.websocket.idle_timeout", 1);
  } else {
    test_server_->waitForCounterGe("http.config_test.downstream_rq_idle_timeout", 1);
  }
  tcp_client_->waitForDisconnect();
  ASSERT_TRUE(fake_tcp_upstream_connection_->waitForDisconnect());
}

TEST_P(WebsocketIntegrationTest, WebSocketLogging) {
  if (!old_style_websockets_)
    return;
  envoy::api::v2::route::RouteAction::WebSocketProxyConfig ws_config;
  ws_config.mutable_idle_timeout()->set_nanos(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(100)).count());

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

  performWebSocketUpgrade(upgrade_req_str_, upgrade_resp_str_);
  sendBidirectionalData();

  tcp_client_->close();
  ASSERT_TRUE(fake_tcp_upstream_connection_->waitForDisconnect());

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

  const std::string upgrade_req_str = createUpgradeRequest("foo");
  const std::string upgrade_resp_str = createUpgradeResponse("foo");

  // Upgrade, send some data and disconnect downstream

  tcp_client_ = makeTcpConnection(lookupPort("http"));
  // Send websocket upgrade request
  // The size of headers received by the destination is 228 bytes.
  tcp_client_->write(upgrade_req_str);
  if (old_style_websockets_) {
    test_server_->waitForCounterGe("tcp.websocket.downstream_cx_total", 1);
  }
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_tcp_upstream_connection_));
  std::string data;
  ASSERT_TRUE(fake_tcp_upstream_connection_->waitForData(&headersRead, &data));
  validateInitialUpstreamData(data);

  // Accept websocket upgrade request
  ASSERT_TRUE(fake_tcp_upstream_connection_->write(upgrade_resp_str));
  tcp_client_->waitForData("\r\n\r\n", false);
  if (old_style_websockets_) {
    ASSERT_EQ(tcp_client_->data(), upgrade_resp_str);
  }

  sendBidirectionalData();

  // downstream disconnect
  tcp_client_->write("bye!");
  tcp_client_->close();
  std::string final_data;
  ASSERT_TRUE(fake_tcp_upstream_connection_->waitForData(
      FakeRawConnection::waitForInexactMatch("bye"), &final_data));
  ASSERT_TRUE(fake_tcp_upstream_connection_->waitForDisconnect());

  const std::string modified_upgrade_resp_str = "HTTP/1.1 101 Switching Protocols\r\nconnection: "
                                                "Upgrade\r\nupgrade: foo\r\ncontent-length: "
                                                "0\r\n\r\n";
  validateFinalDownstreamData(tcp_client_->data(), modified_upgrade_resp_str + "world");
  const std::string upstream_payload = "GET /websocket/test HTTP/1.1\r\n"
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
        createUpgradeRequest("websocket", early_data_req_str.length());
    IntegrationTcpClientPtr tcp_client_ = makeTcpConnection(lookupPort("http"));
    tcp_client_->write(upgrade_req_str + early_data_req_str);
    tcp_client_->waitForData("\r\n\r\n", false);
    EXPECT_NE(tcp_client_->data().find("413"), std::string::npos);
    tcp_client_->waitForDisconnect(true);
  }

  // HTTP requests are configured to disallow large bodies.
  {
    const std::string upgrade_req_str = fmt::format("GET / HTTP/1.1\r\nHost: host\r\nConnection: "
                                                    "keep-alive\r\nContent-Length: {}\r\n\r\n",
                                                    early_data_req_str.length());
    IntegrationTcpClientPtr tcp_client_ = makeTcpConnection(lookupPort("http"));
    tcp_client_->write(upgrade_req_str + early_data_req_str);
    tcp_client_->waitForData("\r\n\r\n", false);
    EXPECT_NE(tcp_client_->data().find("413"), std::string::npos);
    tcp_client_->waitForDisconnect(true);
  }

  // Foo upgrades are configured without the buffer filter, so should explicitly
  // allow large payload.
  {
    const std::string upgrade_req_str = createUpgradeRequest("foo", early_data_req_str.length());
    IntegrationTcpClientPtr tcp_client_ = makeTcpConnection(lookupPort("http"));
    tcp_client_->write(upgrade_req_str + early_data_req_str);
    FakeRawConnectionPtr fake_tcp_upstream_connection_;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_tcp_upstream_connection_));
    // Make sure the full payload arrives.
    ASSERT_TRUE(fake_tcp_upstream_connection_->waitForData(
        FakeRawConnection::waitForInexactMatch(early_data_req_str.c_str())));
    // Tear down all the connections cleanly.
    tcp_client_->close();
    ASSERT_TRUE(fake_tcp_upstream_connection_->waitForDisconnect());
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
  IntegrationTcpClientPtr tcp_client_ = makeTcpConnection(lookupPort("http"));
  tcp_client_->write(upgrade_req_str);
  FakeRawConnectionPtr fake_tcp_upstream_connection_;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_tcp_upstream_connection_));
  ASSERT_TRUE(fake_tcp_upstream_connection_->waitForData(
      FakeRawConnection::waitForInexactMatch("SomeWebSocketPayload")));

  // Finish the upgrade.
  const std::string upgrade_resp_str =
      "HTTP/1.1 101 Switching Protocols\r\nconnection: Upgrade\r\nupgrade: Websocket\r\n"
      "transfer-encoding: chunked\r\n\r\n"
      "4\r\nabcd\r\n0\r\n\r\n"
      "SomeWebsocketResponsePayload";
  ASSERT_TRUE(fake_tcp_upstream_connection_->write(upgrade_resp_str));
  tcp_client_->waitForData("SomeWebsocketResponsePayload", false);

  // Verify bidirectional data still works.
  tcp_client_->write("FinalClientPayload");
  std::string final_data;
  ASSERT_TRUE(fake_tcp_upstream_connection_->waitForData(
      FakeRawConnection::waitForInexactMatch("FinalClientPayload"), &final_data));
  ASSERT_TRUE(fake_tcp_upstream_connection_->write("FinalServerPayload"));
  tcp_client_->waitForData("FinalServerPayload", false);

  // Clean up.
  tcp_client_->close();
  ASSERT_TRUE(fake_tcp_upstream_connection_->waitForDisconnect());

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
  validateFinalDownstreamData(tcp_client_->data(), modified_downstream_payload);
}

} // namespace Envoy
