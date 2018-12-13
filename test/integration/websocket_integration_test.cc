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

} // namespace

void WebsocketIntegrationTest::validateUpgradeRequestHeaders(
    const Http::HeaderMap& original_proxied_request_headers,
    const Http::HeaderMap& original_request_headers) {
  Http::TestHeaderMapImpl proxied_request_headers(original_proxied_request_headers);
  if (proxied_request_headers.ForwardedProto()) {
    ASSERT_STREQ(proxied_request_headers.ForwardedProto()->value().c_str(), "http");
    proxied_request_headers.removeForwardedProto();
  }

  // Check for and remove headers added by default for HTTP requests.
  ASSERT_TRUE(proxied_request_headers.RequestId() != nullptr);
  ASSERT_TRUE(proxied_request_headers.EnvoyExpectedRequestTimeoutMs() != nullptr);
  proxied_request_headers.removeEnvoyExpectedRequestTimeoutMs();

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

  // Check for and remove headers added by default for HTTP responses.
  ASSERT_TRUE(proxied_response_headers.Date() != nullptr);
  ASSERT_TRUE(proxied_response_headers.Server() != nullptr);
  ASSERT_STREQ(proxied_response_headers.Server()->value().c_str(), "envoy");
  proxied_response_headers.removeDate();
  proxied_response_headers.removeServer();

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
                        testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                        HttpProtocolIntegrationTest::protocolTestParamsToString);

ConfigHelper::HttpModifierFunction setRouteUsingWebsocket() {
  return
      [](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm) {
        hcm.add_upgrade_configs()->set_upgrade_type("websocket");
      };
}

void WebsocketIntegrationTest::initialize() {
  if (upstreamProtocol() != FakeHttpConnection::Type::HTTP1) {
    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) -> void {
          auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
          cluster->mutable_http2_protocol_options()->set_allow_connect(true);
        });
  }
  if (downstreamProtocol() != Http::CodecClient::Type::HTTP1) {
    config_helper_.addConfigModifier(
        [&](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm)
            -> void { hcm.mutable_http2_protocol_options()->set_allow_connect(true); });
  }
  HttpProtocolIntegrationTest::initialize();
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
  test_server_->waitForCounterGe("http.config_test.downstream_cx_upgrades_total", 1);
  test_server_->waitForGaugeGe("http.config_test.downstream_cx_upgrades_active", 1);

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
  config_helper_.addConfigModifier(setRouteUsingWebsocket());
  initialize();

  performUpgrade(upgradeRequestHeaders(), upgradeResponseHeaders());
  sendBidirectionalData();

  // Send some final data from the client, and disconnect.
  codec_client_->sendData(*request_encoder_, "bye!", false);
  codec_client_->close();

  // Verify the final data was received and that the connection is torn down.
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, "hellobye!"));

  ASSERT_TRUE(waitForUpstreamDisconnectOrReset());
  test_server_->waitForGaugeEq("http.config_test.downstream_cx_upgrades_active", 0);
}

TEST_P(WebsocketIntegrationTest, WebSocketConnectionUpstreamDisconnect) {
  config_helper_.addConfigModifier(setRouteUsingWebsocket());
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
  config_helper_.addConfigModifier(setRouteUsingWebsocket());
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
  config_helper_.addConfigModifier(setRouteUsingWebsocket());
  config_helper_.addConfigModifier(
      [&](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm)
          -> void {
        auto* route_config = hcm.mutable_route_config();
        auto* virtual_host = route_config->mutable_virtual_hosts(0);
        auto* route = virtual_host->mutable_routes(0)->mutable_route();
        route->mutable_idle_timeout()->set_seconds(0);
        route->mutable_idle_timeout()->set_nanos(200 * 1000 * 1000);
      });
  initialize();

  // WebSocket upgrade, send some data and disconnect downstream
  performUpgrade(upgradeRequestHeaders(), upgradeResponseHeaders());
  sendBidirectionalData();

  test_server_->waitForCounterGe("http.config_test.downstream_rq_idle_timeout", 1);
  waitForClientDisconnectOrReset();
  ASSERT_TRUE(waitForUpstreamDisconnectOrReset());
}

// Technically not a websocket tests, but verfies normal upgrades have parity
// with websocket upgrades
TEST_P(WebsocketIntegrationTest, NonWebsocketUpgrade) {
  config_helper_.addConfigModifier(
      [&](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm)
          -> void {
        auto* foo_upgrade = hcm.add_upgrade_configs();
        foo_upgrade->set_upgrade_type("foo");
      });

  config_helper_.addConfigModifier(setRouteUsingWebsocket());
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

TEST_P(WebsocketIntegrationTest, RouteSpecificUpgrade) {
  config_helper_.addConfigModifier(
      [&](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm)
          -> void {
        auto* foo_upgrade = hcm.add_upgrade_configs();
        foo_upgrade->set_upgrade_type("foo");
        foo_upgrade->mutable_enabled()->set_value(false);
      });
  config_helper_.addRoute("host", "/websocket/test", "cluster_0", false,
                          envoy::api::v2::route::RouteAction::NOT_FOUND,
                          envoy::api::v2::route::VirtualHost::NONE, {}, false, "foo");
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
  config_helper_.addConfigModifier(setRouteUsingWebsocket());

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

  config_helper_.addConfigModifier(setRouteUsingWebsocket());
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
