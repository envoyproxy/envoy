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

Http::TestHeaderMapImpl upgradeRequestHeaders(const char* upgrade_type = "websocket",
                                              uint32_t content_length = 0) {
  return Http::TestHeaderMapImpl{
      {":authority", "host"},       {"content-length", fmt::format("{}", content_length)},
      {":path", "/websocket/test"}, {":method", "GET"},
      {"upgrade", upgrade_type},    {"connection", "keep-alive, Upgrade"}};
}

Http::TestHeaderMapImpl upgradeResponseHeaders(const char* upgrade_type = "websocket") {
  return Http::TestHeaderMapImpl{{":status", "101"},
                                 {"connection", "Upgrade"},
                                 {"upgrade", upgrade_type},
                                 {"content-length", "0"}};
}

void validateUpgradeResponseHeaders(const Http::HeaderMap& original_proxied_response_headers,
                                    Http::TestHeaderMapImpl& original_response_headers,
                                    bool old_style) {
  Http::TestHeaderMapImpl proxied_response_headers(original_proxied_response_headers);
  if (!old_style) {
    ASSERT_TRUE(proxied_response_headers.Date() != nullptr);
    ASSERT_TRUE(proxied_response_headers.Server() != nullptr);
    ASSERT_STREQ(proxied_response_headers.Server()->value().c_str(), "envoy");

    proxied_response_headers.removeDate();
    proxied_response_headers.removeServer();
  } else {
    // The upgrade response doesn't currently include a content length.
    original_response_headers.removeContentLength();
  }

  EXPECT_EQ(proxied_response_headers, original_response_headers);
}

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

void WebsocketIntegrationTest::validateInitialUpstreamData(const std::string& received_data,
                                                           bool initial_headers_chunked) {
  // In HTTP1, the transfer-length is defined by use of the "chunked" transfer-coding, even if
  // content-length header is present. No body websocket upgrade request send to upstream has
  // content-length header and has no transfer-encoding header.
  EXPECT_EQ(!initial_headers_chunked, received_data.find("content-length:") == std::string::npos);
  EXPECT_EQ(initial_headers_chunked, received_data.find("transfer-encoding:") == std::string::npos);
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

void WebsocketIntegrationTest::performWebSocketUpgrade(
    const Http::TestHeaderMapImpl& upgrade_request_headers,
    const std::string& upgrade_resp_string) {
  // Establish the initial connection.
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send websocket upgrade request
  auto encoder_decoder = codec_client_->startRequest(upgrade_request_headers);
  request_encoder_ = &encoder_decoder.first;
  response_ = std::move(encoder_decoder.second);
  if (old_style_websockets_) {
    test_server_->waitForCounterGe("tcp.websocket.downstream_cx_total", 1);
  }

  // Verify the upgrade was received upstream.
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_tcp_upstream_connection_));
  std::string data;
  ASSERT_TRUE(fake_tcp_upstream_connection_->waitForData(&headersRead, &data));
  validateInitialUpstreamData(data, upgrade_request_headers.TransferEncoding() != nullptr);

  // Send the upgrade response
  ASSERT_TRUE(fake_tcp_upstream_connection_->write(upgrade_resp_string));

  // Verify the upgrade response was received downstream.
  auto upgrade_response_headers(
      upgradeResponseHeaders(upgrade_request_headers.Upgrade()->value().c_str()));
  if (upgrade_resp_string.find("transfer-encoding:") != std::string::npos) {
    upgrade_response_headers.removeContentLength();
    upgrade_response_headers.insertTransferEncoding().value().append("chunked", 7);
  }
  response_->waitForHeaders();
  validateUpgradeResponseHeaders(response_->headers(), upgrade_response_headers,
                                 old_style_websockets_);
}

void WebsocketIntegrationTest::sendBidirectionalData() {
  // Verify that the client can still send data upstream, and that upstream
  // receives it.
  codec_client_->sendData(*request_encoder_, "hello", false);
  ASSERT_TRUE(
      fake_tcp_upstream_connection_->waitForData(FakeRawConnection::waitForInexactMatch("hello")));

  // Verify the upstream can send data to the client and that the client
  // receives it.
  ASSERT_TRUE(fake_tcp_upstream_connection_->write("world"));
  response_->waitForBodyData(5);
  EXPECT_EQ("world", response_->body());
}

TEST_P(WebsocketIntegrationTest, WebSocketConnectionDownstreamDisconnect) {
  config_helper_.addConfigModifier(setRouteUsingWebsocket(nullptr, old_style_websockets_));
  initialize();

  performWebSocketUpgrade(upgradeRequestHeaders(), upgrade_resp_str_);
  sendBidirectionalData();

  // Send some final data from the client, and disconnect.
  codec_client_->sendData(*request_encoder_, "bye!", false);
  codec_client_->close();

  // Verify the final data was received and that the connection is torn down.
  std::string final_data;
  ASSERT_TRUE(fake_tcp_upstream_connection_->waitForData(
      FakeRawConnection::waitForInexactMatch("bye"), &final_data));
  ASSERT_TRUE(fake_tcp_upstream_connection_->waitForDisconnect());

  if (old_style_websockets_) {
    return;
  }

  const std::string upstream_payload = "GET /websocket/test HTTP/1.1\r\n"
                                       "host: host\r\n"
                                       "content-length: 0\r\n"
                                       "upgrade: websocket\r\n"
                                       "connection: keep-alive, Upgrade\r\n"
                                       "x-forwarded-proto: http\r\n"
                                       "x-envoy-expected-rq-timeout-ms: 15000\r\n\r\n"
                                       "hellobye!";
  validateFinalUpstreamData(final_data, upstream_payload);
}

TEST_P(WebsocketIntegrationTest, WebSocketConnectionUpstreamDisconnect) {
  config_helper_.addConfigModifier(setRouteUsingWebsocket(nullptr, old_style_websockets_));
  initialize();

  performWebSocketUpgrade(upgradeRequestHeaders(), upgrade_resp_str_);

  // Standard TCP proxy semantics post upgrade
  codec_client_->sendData(*request_encoder_, "hello", false);
  ASSERT_TRUE(
      fake_tcp_upstream_connection_->waitForData(FakeRawConnection::waitForInexactMatch("hello")));

  // Send data downstream and disconnect immediately.
  ASSERT_TRUE(fake_tcp_upstream_connection_->write("world"));
  ASSERT_TRUE(fake_tcp_upstream_connection_->close());
  ASSERT_TRUE(fake_tcp_upstream_connection_->waitForDisconnect());

  // Verify both the data and the disconnect went through.
  response_->waitForBodyData(5);
  EXPECT_EQ("world", response_->body());
  codec_client_->waitForDisconnect();
  ASSERT(!fake_tcp_upstream_connection_->connected());
}

TEST_P(WebsocketIntegrationTest, EarlyData) {
  config_helper_.addConfigModifier(setRouteUsingWebsocket(nullptr, old_style_websockets_));
  initialize();

  // Establish the initial connection.
  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string early_data_req_str = "hello";
  const std::string early_data_resp_str = "world";

  // Send websocket upgrade request with early data.
  auto encoder_decoder =
      codec_client_->startRequest(upgradeRequestHeaders("websocket", early_data_req_str.size()));
  request_encoder_ = &encoder_decoder.first;
  response_ = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, early_data_req_str, false);

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

  response_->waitForHeaders();
  auto upgrade_response_headers(upgradeResponseHeaders());
  validateUpgradeResponseHeaders(response_->headers(), upgrade_response_headers,
                                 old_style_websockets_);
  response_->waitForBodyData(5);
  codec_client_->waitForDisconnect();
  EXPECT_EQ("world", response_->body());
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
  performWebSocketUpgrade(upgradeRequestHeaders(), upgrade_resp_str_);
  sendBidirectionalData();

  if (old_style_websockets_) {
    test_server_->waitForCounterGe("tcp.websocket.idle_timeout", 1);
  } else {
    test_server_->waitForCounterGe("http.config_test.downstream_rq_idle_timeout", 1);
  }
  codec_client_->waitForDisconnect();
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

  performWebSocketUpgrade(upgradeRequestHeaders(), upgrade_resp_str_);
  sendBidirectionalData();

  codec_client_->close();
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

  performWebSocketUpgrade(upgradeRequestHeaders("foo", 0), createUpgradeResponse("foo"));
  sendBidirectionalData();

  // downstream disconnect
  codec_client_->sendData(*request_encoder_, "bye!", false);
  codec_client_->close();
  std::string final_data;
  ASSERT_TRUE(fake_tcp_upstream_connection_->waitForData(
      FakeRawConnection::waitForInexactMatch("bye"), &final_data));
  ASSERT_TRUE(fake_tcp_upstream_connection_->waitForDisconnect());

  auto upgrade_response_headers(upgradeResponseHeaders("foo"));
  validateUpgradeResponseHeaders(response_->headers(), upgrade_response_headers,
                                 old_style_websockets_);
  const std::string upstream_payload = "GET /websocket/test HTTP/1.1\r\n"
                                       "host: host\r\n"
                                       "content-length: 0\r\n"
                                       "upgrade: foo\r\n"
                                       "connection: keep-alive, Upgrade\r\n"
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
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto encoder_decoder = codec_client_->startRequest(
        upgradeRequestHeaders("websocket", early_data_req_str.length()));
    response_ = std::move(encoder_decoder.second);
    codec_client_->sendData(encoder_decoder.first, early_data_req_str, false);
    response_->waitForEndStream();
    EXPECT_STREQ("413", response_->headers().Status()->value().c_str());
    codec_client_->waitForDisconnect();
  }

  // HTTP requests are configured to disallow large bodies.
  {
    Http::TestHeaderMapImpl request_headers{
        {":method", "GET"}, {":path", "/"}, {"content-length", "2048"}, {":authority", "host"}};
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto encoder_decoder = codec_client_->startRequest(request_headers);
    response_ = std::move(encoder_decoder.second);
    codec_client_->sendData(encoder_decoder.first, early_data_req_str, false);
    response_->waitForEndStream();
    EXPECT_STREQ("413", response_->headers().Status()->value().c_str());
    codec_client_->waitForDisconnect();
  }

  // Foo upgrades are configured without the buffer filter, so should explicitly
  // allow large payload.
  {
    performWebSocketUpgrade(upgradeRequestHeaders("foo", early_data_req_str.length()),
                            createUpgradeResponse("foo"));
    codec_client_->sendData(*request_encoder_, early_data_req_str, false);
    ASSERT_TRUE(fake_tcp_upstream_connection_->waitForData(
        FakeRawConnection::waitForInexactMatch(early_data_req_str.c_str())));

    // Tear down all the connections cleanly.
    codec_client_->close();
    ASSERT_TRUE(fake_tcp_upstream_connection_->waitForDisconnect());
  }
}

TEST_P(WebsocketIntegrationTest, BidirectionalChunkedData) {
  config_helper_.addConfigModifier(setRouteUsingWebsocket(nullptr, old_style_websockets_));
  initialize();

  auto request_headers = Http::TestHeaderMapImpl{{":authority", "host"},
                                                 {":path", "/websocket/test"},
                                                 {":method", "GET"},
                                                 {"upgrade", "websocket"},
                                                 {"connection", "keep-alive, Upgrade"}};
  const std::string upgrade_resp_str =
      "HTTP/1.1 101 Switching Protocols\r\nconnection: Upgrade\r\nupgrade: websocket\r\n"
      "transfer-encoding: chunked\r\n\r\n"
      "4\r\nabcd\r\n0\r\n\r\n"
      "SomeWebsocketResponsePayload";
  performWebSocketUpgrade(request_headers, upgrade_resp_str);

  codec_client_->sendData(*request_encoder_, "3\r\n123\r\n0\r\n\r\nSomeWebSocketPayload", false);
  ASSERT_TRUE(fake_tcp_upstream_connection_->waitForData(
      FakeRawConnection::waitForInexactMatch("SomeWebSocketPayload")));

  std::string response_payload = "4\r\nabcd\r\n0\r\n\r\nSomeWebsocketResponsePayload";
  if (response_->body().size() != response_payload.size()) {
    response_->waitForBodyData(response_payload.size());
  }
  EXPECT_EQ(response_payload, response_->body());

  // Verify bidirectional data still works.
  codec_client_->sendData(*request_encoder_, "FinalClientPayload", false);
  ASSERT_TRUE(fake_tcp_upstream_connection_->waitForData(
      FakeRawConnection::waitForInexactMatch("FinalClientPayload")));

  // Verify the upstream can send data to the client and that the client
  // receives it.
  ASSERT_TRUE(fake_tcp_upstream_connection_->write("FinalServerPayload"));
  response_->waitForBodyData(5);
  EXPECT_EQ(response_payload + "FinalServerPayload", response_->body());

  // Clean up.
  codec_client_->close();
  ASSERT_TRUE(fake_tcp_upstream_connection_->waitForDisconnect());
}

} // namespace Envoy
