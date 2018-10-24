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
  return Http::TestHeaderMapImpl{{":authority", "host"},
                                 {"content-length", fmt::format("{}", content_length)},
                                 {":path", "/websocket/test"},
                                 {":method", "GET"},
                                 {":scheme", "http"},
                                 {"upgrade", upgrade_type},
                                 {"connection", "keep-alive, upgrade"}};
}

Http::TestHeaderMapImpl upgradeResponseHeaders(const char* upgrade_type = "websocket") {
  return Http::TestHeaderMapImpl{{":status", "101"},
                                 {"connection", "upgrade"},
                                 {"upgrade", upgrade_type},
                                 {"content-length", "0"}};
}

std::string
websocketTestParamsToString(const ::testing::TestParamInfo<WebsocketProtocolTestParams>& params) {
  return absl::StrCat(
      (params.param.version == Network::Address::IpVersion::v4 ? "IPv4_" : "IPv6_"),
      (params.param.downstream_protocol == Http::CodecClient::Type::HTTP2 ? "Http2Downstream_"
                                                                          : "HttpDownstream_"),
      (params.param.upstream_protocol == FakeHttpConnection::Type::HTTP2 ? "Http2Upstream"
                                                                         : "HttpUpstream"),
      params.param.old_style == true ? "_Old" : "_New");
}

std::vector<WebsocketProtocolTestParams> getWebsocketTestParams() {
  const std::vector<Http::CodecClient::Type> downstream_protocols = {
      Http::CodecClient::Type::HTTP1, Http::CodecClient::Type::HTTP2};
  const std::vector<FakeHttpConnection::Type> upstream_protocols = {
      FakeHttpConnection::Type::HTTP1, FakeHttpConnection::Type::HTTP2};
  std::vector<WebsocketProtocolTestParams> ret;

  for (auto ip_version : TestEnvironment::getIpVersionsForTest()) {
    for (auto downstream_protocol : downstream_protocols) {
      for (auto upstream_protocol : upstream_protocols) {
        ret.push_back(
            WebsocketProtocolTestParams{ip_version, downstream_protocol, upstream_protocol, false});
      }
    }
  }
  for (auto ip_version : TestEnvironment::getIpVersionsForTest()) {
    ret.push_back(WebsocketProtocolTestParams{ip_version, Http::CodecClient::Type::HTTP1,
                                              FakeHttpConnection::Type::HTTP1, true});
  }
  return ret;
}

} // namespace

void WebsocketIntegrationTest::validateUpgradeRequestHeaders(
    const Http::HeaderMap& original_proxied_request_headers,
    const Http::HeaderMap& original_request_headers) {
  Http::TestHeaderMapImpl proxied_request_headers(original_proxied_request_headers);
  if (proxied_request_headers.ForwardedProto()) {
    ASSERT_STREQ(proxied_request_headers.ForwardedProto()->value().c_str(), "http");
    proxied_request_headers.removeForwardedProto();
  }

  if (!old_style_websockets_) {
    // Check for and remove headers added by default for HTTP requests.
    ASSERT_TRUE(proxied_request_headers.RequestId() != nullptr);
    ASSERT_TRUE(proxied_request_headers.EnvoyExpectedRequestTimeoutMs() != nullptr);
    proxied_request_headers.removeEnvoyExpectedRequestTimeoutMs();
  } else {
    // Check for and undo the path rewrite.
    ASSERT_STREQ(proxied_request_headers.Path()->value().c_str(), "/websocket");
    proxied_request_headers.Path()->value().append("/test", 5);
    ASSERT_STREQ(proxied_request_headers.EnvoyOriginalPath()->value().c_str(), "/websocket/test");
    proxied_request_headers.removeEnvoyOriginalPath();
  }
  if (proxied_request_headers.Scheme()) {
    ASSERT_STREQ(proxied_request_headers.Scheme()->value().c_str(), "http");
  } else {
    proxied_request_headers.insertScheme().value().append("http", 4);
  }
  commonValidate(proxied_request_headers, original_request_headers);
  proxied_request_headers.removeRequestId();

  EXPECT_THAT(&proxied_request_headers, HeaderMapEqualIgnoreOrder(&original_request_headers));
}

void WebsocketIntegrationTest::validateUpgradeResponseHeaders(
    const Http::HeaderMap& original_proxied_response_headers,
    const Http::HeaderMap& original_response_headers) {
  Http::TestHeaderMapImpl proxied_response_headers(original_proxied_response_headers);
  if (!old_style_websockets_) {
    // Check for and remove headers added by default for HTTP responses.
    ASSERT_TRUE(proxied_response_headers.Date() != nullptr);
    ASSERT_TRUE(proxied_response_headers.Server() != nullptr);
    ASSERT_STREQ(proxied_response_headers.Server()->value().c_str(), "envoy");
    proxied_response_headers.removeDate();
    proxied_response_headers.removeServer();
  }
  commonValidate(proxied_response_headers, original_response_headers);

  EXPECT_THAT(&proxied_response_headers, HeaderMapEqualIgnoreOrder(&original_response_headers));
}

void WebsocketIntegrationTest::commonValidate(Http::HeaderMap& proxied_headers,
                                              const Http::HeaderMap& original_headers) {
  // 0 byte content lengths may be stripped on the H2 path - ignore that as a difference by adding
  // it back to the proxied headers.
  if (original_headers.ContentLength() && proxied_headers.ContentLength() == nullptr) {
    proxied_headers.insertContentLength().value(size_t(0));
  }
  // If no content length is specified, the HTTP1 codec will add a chunked encoding header.
  if (original_headers.ContentLength() == nullptr &&
      proxied_headers.TransferEncoding() != nullptr) {
    ASSERT_STREQ(proxied_headers.TransferEncoding()->value().c_str(), "chunked");
    proxied_headers.removeTransferEncoding();
  }
  if (proxied_headers.Connection() != nullptr &&
      proxied_headers.Connection()->value() == "upgrade" &&
      original_headers.Connection() != nullptr &&
      original_headers.Connection()->value() == "keep-alive, upgrade") {
    // The keep-alive is implicit for HTTP/1.1, so Enovy only sets the upgrade
    // header when converting from HTTP/1.1 to H2
    proxied_headers.Connection()->value().setCopy("keep-alive, upgrade", 19);
  }
}

INSTANTIATE_TEST_CASE_P(Protocols, WebsocketIntegrationTest,
                        testing::ValuesIn(getWebsocketTestParams()), websocketTestParamsToString);

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
  } else {
    if (upstreamProtocol() != FakeHttpConnection::Type::HTTP1) {
      config_helper_.addConfigModifier(
          [&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) -> void {
            auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
            cluster->mutable_http2_protocol_options()->set_allow_connect(true);
          });
    }
    if (downstreamProtocol() != Http::CodecClient::Type::HTTP1) {
      config_helper_.addConfigModifier(
          [&](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager&
                  hcm) -> void { hcm.mutable_http2_protocol_options()->set_allow_connect(true); });
    }
  }
  HttpIntegrationTest::initialize();
}

void WebsocketIntegrationTest::performUpgrade(
    const Http::TestHeaderMapImpl& upgrade_request_headers,
    const Http::TestHeaderMapImpl& upgrade_response_headers) {
  // Establish the initial connection.
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send websocket upgrade request
  auto encoder_decoder = codec_client_->startRequest(upgrade_request_headers);
  request_encoder_ = &encoder_decoder.first;
  response_ = std::move(encoder_decoder.second);
  if (old_style_websockets_) {
    test_server_->waitForCounterGe("tcp.websocket.downstream_cx_total", 1);
  } else {
    test_server_->waitForCounterGe("http.config_test.downstream_cx_upgrades_total", 1);
    test_server_->waitForGaugeGe("http.config_test.downstream_cx_upgrades_active", 1);
  }

  // Verify the upgrade was received upstream.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  validateUpgradeRequestHeaders(upstream_request_->headers(), upgrade_request_headers);

  // Send the upgrade response
  upstream_request_->encodeHeaders(upgrade_response_headers, false);

  // Verify the upgrade response was received downstream.
  response_->waitForHeaders();
  validateUpgradeResponseHeaders(response_->headers(), upgrade_response_headers);
}

void WebsocketIntegrationTest::sendBidirectionalData() {
  // Verify that the client can still send data upstream, and that upstream
  // receives it.
  codec_client_->sendData(*request_encoder_, "hello", false);
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, "hello"));

  // Verify the upstream can send data to the client and that the client
  // receives it.
  upstream_request_->encodeData("world", false);
  response_->waitForBodyData(5);
  EXPECT_EQ("world", response_->body());
}

TEST_P(WebsocketIntegrationTest, WebSocketConnectionDownstreamDisconnect) {
  config_helper_.addConfigModifier(setRouteUsingWebsocket(nullptr, old_style_websockets_));
  initialize();

  performUpgrade(upgradeRequestHeaders(), upgradeResponseHeaders());
  sendBidirectionalData();

  // Send some final data from the client, and disconnect.
  codec_client_->sendData(*request_encoder_, "bye!", false);
  codec_client_->close();

  // Verify the final data was received and that the connection is torn down.
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, "hellobye!"));

  ASSERT_TRUE(waitForUpstreamDisconnectOrReset());
  if (!old_style_websockets_) {
    test_server_->waitForGaugeEq("http.config_test.downstream_cx_upgrades_active", 0);
  }
}

TEST_P(WebsocketIntegrationTest, WebSocketConnectionUpstreamDisconnect) {
  config_helper_.addConfigModifier(setRouteUsingWebsocket(nullptr, old_style_websockets_));
  initialize();

  performUpgrade(upgradeRequestHeaders(), upgradeResponseHeaders());

  // Standard TCP proxy semantics post upgrade
  codec_client_->sendData(*request_encoder_, "hello", false);
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, "hello"));

  // Send data downstream and disconnect immediately.
  upstream_request_->encodeData("world", false);
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  // Verify both the data and the disconnect went through.
  response_->waitForBodyData(5);
  EXPECT_EQ("world", response_->body());
  waitForClientDisconnectOrReset();
}

TEST_P(WebsocketIntegrationTest, EarlyData) {
  if (downstreamProtocol() == Http::CodecClient::Type::HTTP2 ||
      upstreamProtocol() == FakeHttpConnection::Type::HTTP2) {
    return;
  }
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

  // Wait for both the upgrade, and the early data.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, "hello"));

  // Accept websocket upgrade request
  upstream_request_->encodeHeaders(upgradeResponseHeaders(), false);
  // Reply also with early data
  upstream_request_->encodeData(early_data_resp_str, false);
  // upstream disconnect
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  response_->waitForHeaders();
  auto upgrade_response_headers(upgradeResponseHeaders());
  validateUpgradeResponseHeaders(response_->headers(), upgrade_response_headers);

  if (downstreamProtocol() == Http::CodecClient::Type::HTTP1) {
    // For H2, the disconnect may result in the terminal data not being proxied.
    response_->waitForBodyData(5);
  }
  waitForClientDisconnectOrReset();
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
  performUpgrade(upgradeRequestHeaders(), upgradeResponseHeaders());
  sendBidirectionalData();

  if (old_style_websockets_) {
    test_server_->waitForCounterGe("tcp.websocket.idle_timeout", 1);
  } else {
    test_server_->waitForCounterGe("http.config_test.downstream_rq_idle_timeout", 1);
  }
  waitForClientDisconnectOrReset();
  ASSERT_TRUE(waitForUpstreamDisconnectOrReset());
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

  performUpgrade(upgradeRequestHeaders(), upgradeResponseHeaders());
  sendBidirectionalData();

  codec_client_->close();
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  std::string log_result;
  do {
    log_result = Filesystem::fileReadToEnd(access_log_path);
  } while (log_result.empty());

  const std::string ip_port_regex = (version_ == Network::Address::IpVersion::v4)
                                        ? R"EOF(127\.0\.0\.1:[0-9]+)EOF"
                                        : R"EOF(\[::1\]:[0-9]+)EOF";

  EXPECT_THAT(log_result, MatchesRegex(fmt::format(expected_log_template,
                                                   101, // response length
                                                   5,   // hello length
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

  performUpgrade(upgradeRequestHeaders("foo", 0), upgradeResponseHeaders("foo"));
  sendBidirectionalData();
  codec_client_->sendData(*request_encoder_, "bye!", false);
  if (downstreamProtocol() == Http::CodecClient::Type::HTTP1) {
    codec_client_->close();
  } else {
    codec_client_->sendReset(*request_encoder_);
  }

  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, "hellobye!"));
  ASSERT_TRUE(waitForUpstreamDisconnectOrReset());

  auto upgrade_response_headers(upgradeResponseHeaders("foo"));
  validateUpgradeResponseHeaders(response_->headers(), upgrade_response_headers);
  codec_client_->close();
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
  const std::string large_req_str(2048, 'a');
  {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto encoder_decoder = codec_client_->startRequest(upgradeRequestHeaders("websocket"));
    response_ = std::move(encoder_decoder.second);
    codec_client_->sendData(encoder_decoder.first, large_req_str, false);
    response_->waitForEndStream();
    EXPECT_STREQ("413", response_->headers().Status()->value().c_str());
    waitForClientDisconnectOrReset();
    codec_client_->close();
  }

  // HTTP requests are configured to disallow large bodies.
  {
    Http::TestHeaderMapImpl request_headers{{":method", "GET"},
                                            {":path", "/"},
                                            {"content-length", "2048"},
                                            {":authority", "host"},
                                            {":scheme", "https"}};
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto encoder_decoder = codec_client_->startRequest(request_headers);
    response_ = std::move(encoder_decoder.second);
    codec_client_->sendData(encoder_decoder.first, large_req_str, false);
    response_->waitForEndStream();
    EXPECT_STREQ("413", response_->headers().Status()->value().c_str());
    waitForClientDisconnectOrReset();
    codec_client_->close();
  }

  // Foo upgrades are configured without the buffer filter, so should explicitly
  // allow large payload.
  if (downstreamProtocol() != Http::CodecClient::Type::HTTP2) {
    performUpgrade(upgradeRequestHeaders("foo"), upgradeResponseHeaders("foo"));
    codec_client_->sendData(*request_encoder_, large_req_str, false);
    ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, large_req_str));

    // Tear down all the connections cleanly.
    codec_client_->close();
    ASSERT_TRUE(waitForUpstreamDisconnectOrReset());
  }
}

TEST_P(WebsocketIntegrationTest, BidirectionalChunkedData) {
  if (downstreamProtocol() == Http::CodecClient::Type::HTTP2 ||
      upstreamProtocol() == FakeHttpConnection::Type::HTTP2) {
    return;
  }

  config_helper_.addConfigModifier(setRouteUsingWebsocket(nullptr, old_style_websockets_));
  initialize();

  auto request_headers = upgradeRequestHeaders();
  request_headers.removeContentLength();
  auto response_headers = upgradeResponseHeaders();
  response_headers.removeContentLength();
  performUpgrade(request_headers, response_headers);

  // With content-length not present, the HTTP codec will send the request with
  // transfer-encoding: chunked.
  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP1) {
    ASSERT_TRUE(upstream_request_->headers().TransferEncoding() != nullptr);
  }
  if (downstreamProtocol() == Http::CodecClient::Type::HTTP1) {
    ASSERT_TRUE(response_->headers().TransferEncoding() != nullptr);
  }

  // Send both a chunked request body and "websocket" payload.
  std::string request_payload = "3\r\n123\r\n0\r\n\r\nSomeWebsocketRequestPayload";
  codec_client_->sendData(*request_encoder_, request_payload, false);
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, request_payload));

  // Send both a chunked response body and "websocket" payload.
  std::string response_payload = "4\r\nabcd\r\n0\r\n\r\nSomeWebsocketResponsePayload";
  upstream_request_->encodeData(response_payload, false);
  response_->waitForBodyData(response_payload.size());
  EXPECT_EQ(response_payload, response_->body());

  // Verify follow-up bidirectional data still works.
  codec_client_->sendData(*request_encoder_, "FinalClientPayload", false);
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, request_payload + "FinalClientPayload"));
  upstream_request_->encodeData("FinalServerPayload", false);
  response_->waitForBodyData(5);
  EXPECT_EQ(response_payload + "FinalServerPayload", response_->body());

  // Clean up.
  codec_client_->close();
  ASSERT_TRUE(waitForUpstreamDisconnectOrReset());
}

} // namespace Envoy
