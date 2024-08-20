#include "test/integration/websocket_integration_test.h"

#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/protobuf/utility.h"

#include "test/integration/utility.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

Http::TestRequestHeaderMapImpl upgradeRequestHeaders(const char* upgrade_type = "websocket",
                                                     uint32_t content_length = 0) {
  return Http::TestRequestHeaderMapImpl{{":authority", "sni.lyft.com"},
                                        {"content-length", fmt::format("{}", content_length)},
                                        {":path", "/websocket/test"},
                                        {":method", "GET"},
                                        {":scheme", "http"},
                                        {"upgrade", upgrade_type},
                                        {"connection", "keep-alive, upgrade"}};
}

Http::TestResponseHeaderMapImpl upgradeResponseHeaders(const char* upgrade_type = "websocket") {
  return Http::TestResponseHeaderMapImpl{
      {":status", "101"}, {"connection", "upgrade"}, {"upgrade", upgrade_type}};
}

template <class ProxiedHeaders, class OriginalHeaders>
void commonValidate(ProxiedHeaders& proxied_headers, const OriginalHeaders& original_headers) {
  // If no content length is specified, the HTTP1 codec will add a chunked encoding header.
  if (original_headers.ContentLength() == nullptr &&
      proxied_headers.TransferEncoding() != nullptr) {
    ASSERT_EQ(proxied_headers.getTransferEncodingValue(), "chunked");
    proxied_headers.removeTransferEncoding();
  }
  if (proxied_headers.Connection() != nullptr &&
      proxied_headers.Connection()->value() == "upgrade" &&
      original_headers.Connection() != nullptr &&
      original_headers.Connection()->value() == "keep-alive, upgrade") {
    // The keep-alive is implicit for HTTP/1.1, so Envoy only sets the upgrade
    // header when converting from HTTP/1.1 to H2
    proxied_headers.setConnection("keep-alive, upgrade");
  }
}

} // namespace

void WebsocketIntegrationTest::validateUpgradeRequestHeaders(
    const Http::RequestHeaderMap& original_proxied_request_headers,
    const Http::RequestHeaderMap& original_request_headers) {
  Http::TestRequestHeaderMapImpl proxied_request_headers(original_proxied_request_headers);
  if (proxied_request_headers.ForwardedProto()) {
    ASSERT_EQ(proxied_request_headers.getForwardedProtoValue(),
              downstreamProtocol() == Http::CodecType::HTTP3 ? "https" : "http");
    proxied_request_headers.removeForwardedProto();
  }

  // Check for and remove headers added by default for HTTP requests.
  ASSERT_TRUE(proxied_request_headers.RequestId() != nullptr);
  ASSERT_TRUE(proxied_request_headers.EnvoyExpectedRequestTimeoutMs() != nullptr);
  proxied_request_headers.removeEnvoyExpectedRequestTimeoutMs();

  if (proxied_request_headers.Scheme()) {
    ASSERT_EQ(proxied_request_headers.getSchemeValue(), "http");
  } else {
    proxied_request_headers.setScheme("http");
  }

  // 0 byte content lengths may be stripped on the H2 path - ignore that as a difference by adding
  // it back to the proxied headers.
  if (original_request_headers.ContentLength() &&
      proxied_request_headers.ContentLength() == nullptr) {
    proxied_request_headers.setContentLength(size_t(0));
  }

  commonValidate(proxied_request_headers, original_request_headers);
  proxied_request_headers.removeRequestId();

  EXPECT_THAT(&proxied_request_headers, HeaderMapEqualIgnoreOrder(&original_request_headers));
}

void WebsocketIntegrationTest::validateUpgradeResponseHeaders(
    const Http::ResponseHeaderMap& original_proxied_response_headers,
    const Http::ResponseHeaderMap& original_response_headers) {
  Http::TestResponseHeaderMapImpl proxied_response_headers(original_proxied_response_headers);

  // Check for and remove headers added by default for HTTP responses.
  ASSERT_TRUE(proxied_response_headers.Date() != nullptr);
  ASSERT_TRUE(proxied_response_headers.Server() != nullptr);
  ASSERT_EQ(proxied_response_headers.getServerValue(), "envoy");
  proxied_response_headers.removeDate();
  proxied_response_headers.removeServer();

  ASSERT_TRUE(proxied_response_headers.TransferEncoding() == nullptr);

  commonValidate(proxied_response_headers, original_response_headers);

  EXPECT_THAT(&proxied_response_headers, HeaderMapEqualIgnoreOrder(&original_response_headers));
}

INSTANTIATE_TEST_SUITE_P(Protocols, WebsocketIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

ConfigHelper::HttpModifierFunction setRouteUsingWebsocket() {
  return [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) { hcm.add_upgrade_configs()->set_upgrade_type("websocket"); };
}

void WebsocketIntegrationTest::initialize() {
  if (upstreamProtocol() == Http::CodecType::HTTP2) {
    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          ConfigHelper::HttpProtocolOptions protocol_options;
          protocol_options.mutable_explicit_http_config()
              ->mutable_http2_protocol_options()
              ->set_allow_connect(true);
          ConfigHelper::setProtocolOptions(
              *bootstrap.mutable_static_resources()->mutable_clusters(0), protocol_options);
        });
  } else if (upstreamProtocol() == Http::CodecType::HTTP3) {
    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          ConfigHelper::HttpProtocolOptions protocol_options;
          protocol_options.mutable_explicit_http_config()
              ->mutable_http3_protocol_options()
              ->set_allow_extended_connect(true);
          ConfigHelper::setProtocolOptions(
              *bootstrap.mutable_static_resources()->mutable_clusters(0), protocol_options);
        });
  }

  if (downstreamProtocol() == Http::CodecType::HTTP2) {
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void { hcm.mutable_http2_protocol_options()->set_allow_connect(true); });
  } else if (downstreamProtocol() == Http::CodecType::HTTP3) {
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void {
          hcm.mutable_http3_protocol_options()->set_allow_extended_connect(true);
        });
  }
  HttpProtocolIntegrationTest::initialize();
}

void WebsocketIntegrationTest::performUpgrade(
    const Http::TestRequestHeaderMapImpl& upgrade_request_headers,
    const Http::TestResponseHeaderMapImpl& upgrade_response_headers, bool upgrade_should_fail) {
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
  if (!upgrade_should_fail) {
    validateUpgradeResponseHeaders(response_->headers(), upgrade_response_headers);
  }
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

TEST_P(WebsocketIntegrationTest, PortStrippingForHttp2) {
  if (downstreamProtocol() != Http::CodecType::HTTP2 ||
      upstreamProtocol() == Http::CodecType::HTTP3) {
    return;
  }

  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) { hcm.set_strip_any_host_port(true); });

  config_helper_.addConfigModifier(setRouteUsingWebsocket());
  initialize();

  performUpgrade(upgradeRequestHeaders(), upgradeResponseHeaders());
  ASSERT_EQ(upstream_request_->headers().getHostValue(), "sni.lyft.com");

  codec_client_->sendData(*request_encoder_, "bye!", false);
  codec_client_->close();
  // Verify the final data was received and that the connection is torn down.
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, "bye!"));
  ASSERT_TRUE(waitForUpstreamDisconnectOrReset());
}

TEST_P(WebsocketIntegrationTest, EarlyData) {
  if (downstreamProtocol() != Http::CodecType::HTTP1 ||
      upstreamProtocol() != Http::CodecType::HTTP1) {
    return;
  }
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.check_switch_protocol_websocket_handshake", "false"}});

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

  if (downstreamProtocol() == Http::CodecType::HTTP1) {
    // For H2, the disconnect may result in the terminal data not being proxied.
    response_->waitForBodyData(5);
  }
  waitForClientDisconnectOrReset();
  EXPECT_EQ("world", response_->body());
}

TEST_P(WebsocketIntegrationTest, WebSocketConnectionIdleTimeout) {
  config_helper_.addConfigModifier(setRouteUsingWebsocket());
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
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

// Technically not a websocket tests, but verifies normal upgrades have parity
// with websocket upgrades
TEST_P(WebsocketIntegrationTest, NonWebsocketUpgrade) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* foo_upgrade = hcm.add_upgrade_configs();
        foo_upgrade->set_upgrade_type("foo");
      });

  config_helper_.addConfigModifier(setRouteUsingWebsocket());
  initialize();

  performUpgrade(upgradeRequestHeaders("foo", 0), upgradeResponseHeaders("foo"));
  sendBidirectionalData();
  codec_client_->sendData(*request_encoder_, "bye!", false);
  if (downstreamProtocol() == Http::CodecType::HTTP1) {
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
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* foo_upgrade = hcm.add_upgrade_configs();
        foo_upgrade->set_upgrade_type("foo");
        foo_upgrade->mutable_enabled()->set_value(false);
      });
  auto host = config_helper_.createVirtualHost("sni.lyft.com", "/websocket/test");
  host.mutable_routes(0)->mutable_route()->add_upgrade_configs()->set_upgrade_type("foo");
  config_helper_.addVirtualHost(host);
  initialize();

  performUpgrade(upgradeRequestHeaders("foo", 0), upgradeResponseHeaders("foo"));
  sendBidirectionalData();
  codec_client_->sendData(*request_encoder_, "bye!", false);
  if (downstreamProtocol() == Http::CodecType::HTTP1) {
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
  // TODO(#26373) - debug and re-enable.
  if (downstreamProtocol() != Http::CodecType::HTTP2 ||
      upstreamProtocol() == Http::CodecType::HTTP3) {
    return;
  }

  config_helper_.addConfigModifier(setRouteUsingWebsocket());

  // Add a small buffer filter to the standard HTTP filter chain. Websocket
  // upgrades will use the HTTP filter chain so will also have small buffers.
  config_helper_.prependFilter(ConfigHelper::smallBufferFilter());

  // Add a second upgrade type which goes directly to the router filter.
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* foo_upgrade = hcm.add_upgrade_configs();
        foo_upgrade->set_upgrade_type("foo");
        auto* filter_list_back = foo_upgrade->add_filters();
        TestUtility::loadFromYaml("name: envoy.filters.http.router", *filter_list_back);
      });
  initialize();

  // Websocket upgrades are configured to disallow large payload.
  const std::string large_req_str(2048, 'a');
  {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto encoder_decoder = codec_client_->startRequest(upgradeRequestHeaders("websocket"));
    response_ = std::move(encoder_decoder.second);
    codec_client_->sendData(encoder_decoder.first, large_req_str, false);
    ASSERT_TRUE(response_->waitForEndStream());
    EXPECT_EQ("413", response_->headers().getStatusValue());
    waitForClientDisconnectOrReset();
    codec_client_->close();
  }

  // HTTP requests are configured to disallow large bodies.
  {
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/"},
                                                   {"content-length", "2048"},
                                                   {":authority", "host"},
                                                   {":scheme", "https"}};
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto encoder_decoder = codec_client_->startRequest(request_headers);
    response_ = std::move(encoder_decoder.second);
    codec_client_->sendData(encoder_decoder.first, large_req_str, false);
    ASSERT_TRUE(response_->waitForEndStream());
    EXPECT_EQ("413", response_->headers().getStatusValue());
    if (downstreamProtocol() != Http::CodecType::HTTP3) {
      waitForClientDisconnectOrReset();
    }
    codec_client_->close();
  }

  // Foo upgrades are configured without the buffer filter, so should explicitly
  // allow large payload.
  if (downstreamProtocol() == Http::CodecType::HTTP1) {
    performUpgrade(upgradeRequestHeaders("foo"), upgradeResponseHeaders("foo"));
    codec_client_->sendData(*request_encoder_, large_req_str, false);
    ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, large_req_str));

    // Tear down all the connections cleanly.
    codec_client_->close();
    ASSERT_TRUE(waitForUpstreamDisconnectOrReset());
  }
}

TEST_P(WebsocketIntegrationTest, BidirectionalNoContentLengthNoTransferEncoding) {
  if (downstreamProtocol() != Http::CodecType::HTTP1 ||
      upstreamProtocol() != Http::CodecType::HTTP1) {
    return;
  }

  config_helper_.addConfigModifier(setRouteUsingWebsocket());
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));

  // Send upgrade request without CL and TE headers
  ASSERT_TRUE(tcp_client->write(
      "GET / HTTP/1.1\r\nHost: host\r\nconnection: upgrade\r\nupgrade: websocket\r\n\r\n", false,
      false));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT(fake_upstream_connection != nullptr);
  std::string received_data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("\r\n\r\n"), &received_data));
  // Make sure Envoy did not add TE or CL headers
  ASSERT_FALSE(absl::StrContains(received_data, "content-length"));
  ASSERT_FALSE(absl::StrContains(received_data, "transfer-encoding"));
  ASSERT_TRUE(fake_upstream_connection->write(
      "HTTP/1.1 101 Switching Protocols\r\nconnection: upgrade\r\nupgrade: websocket\r\n\r\n",
      false));

  tcp_client->waitForData("\r\n\r\n", false);
  // Make sure Envoy did not add TE or CL on the response path
  ASSERT_FALSE(absl::StrContains(tcp_client->data(), "content-length"));
  ASSERT_FALSE(absl::StrContains(tcp_client->data(), "transfer-encoding"));

  fake_upstream_connection->clearData();
  // Send data and make sure Envoy did not add chunk framing
  ASSERT_TRUE(tcp_client->write("foo bar", false, false));
  ASSERT_TRUE(fake_upstream_connection->waitForData(FakeRawConnection::waitForMatch("foo bar")));

  tcp_client->clearData();
  // Send response data and make sure Envoy did not add chunk framing on the response path
  ASSERT_TRUE(fake_upstream_connection->write("bar foo", false));
  tcp_client->waitForData("bar foo");
  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
}

TEST_P(WebsocketIntegrationTest, BidirectionalUpgradeWithTransferEncoding) {
  if (downstreamProtocol() != Http::CodecType::HTTP1 ||
      upstreamProtocol() != Http::CodecType::HTTP1) {
    return;
  }

  config_helper_.addConfigModifier(setRouteUsingWebsocket());
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));

  // Send upgrade request with TE: chunked header
  ASSERT_TRUE(tcp_client->write("GET / HTTP/1.1\r\nHost: host\r\nconnection: upgrade\r\nupgrade: "
                                "websocket\r\ntransfer-encoding: chunked\r\n\r\n",
                                false, false));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT(fake_upstream_connection != nullptr);
  std::string received_data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("\r\n\r\n"), &received_data));
  // Presently Envoy assumes upstream switches protocol and strips TE: chunked before proxying
  // upgrade request to upstream.
  ASSERT_FALSE(absl::StrContains(received_data, "transfer-encoding: chunked"));
  ASSERT_FALSE(absl::StrContains(received_data, "content-length"));
  ASSERT_TRUE(fake_upstream_connection->write(
      "HTTP/1.1 101 Switching Protocols\r\nconnection: upgrade\r\nupgrade: websocket\r\n\r\n",
      false));

  tcp_client->waitForData("\r\n\r\n", false);
  ASSERT_FALSE(absl::StrContains(tcp_client->data(), "content-length"));
  ASSERT_FALSE(absl::StrContains(tcp_client->data(), "transfer-encoding"));

  fake_upstream_connection->clearData();
  // Send data and make sure Envoy did not add chunk framing
  ASSERT_TRUE(tcp_client->write("7\r\nfoo bar\r\n0\r\n\r\n", false, false));
  ASSERT_TRUE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForMatch("7\r\nfoo bar\r\n0\r\n\r\n")));

  tcp_client->clearData();
  // Send response data and make sure Envoy did not add chunk framing on the response path
  ASSERT_TRUE(fake_upstream_connection->write("bar foo", false));
  tcp_client->waitForData("bar foo");
  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
}

TEST_P(WebsocketIntegrationTest, BidirectionalUpgradeWithContentLength) {
  if (downstreamProtocol() != Http::CodecType::HTTP1 ||
      upstreamProtocol() != Http::CodecType::HTTP1) {
    return;
  }

  config_helper_.addConfigModifier(setRouteUsingWebsocket());
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));

  // Send upgrade request with TE: chunked header
  ASSERT_TRUE(tcp_client->write("GET / HTTP/1.1\r\nHost: host\r\nconnection: upgrade\r\nupgrade: "
                                "websocket\r\ncontent-length: 7\r\n\r\n",
                                false, false));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT(fake_upstream_connection != nullptr);
  std::string received_data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("\r\n\r\n"), &received_data));
  // Presently Envoy assumes upstream switches protocol however still proxies CL to upstream.
  ASSERT_TRUE(absl::StrContains(received_data, "content-length: 7"));
  ASSERT_FALSE(absl::StrContains(received_data, "transfer-encoding"));
  ASSERT_TRUE(fake_upstream_connection->write(
      "HTTP/1.1 101 Switching Protocols\r\nconnection: upgrade\r\nupgrade: websocket\r\n\r\n",
      false));

  tcp_client->waitForData("\r\n\r\n", false);
  ASSERT_FALSE(absl::StrContains(tcp_client->data(), "content-length"));
  ASSERT_FALSE(absl::StrContains(tcp_client->data(), "transfer-encoding"));

  fake_upstream_connection->clearData();
  // Send data and make sure Envoy did not add chunk framing
  ASSERT_TRUE(tcp_client->write("foo bar", false, false));
  ASSERT_TRUE(fake_upstream_connection->waitForData(FakeRawConnection::waitForMatch("foo bar")));

  tcp_client->clearData();
  // Send response data and make sure Envoy did not add chunk framing on the response path
  ASSERT_TRUE(fake_upstream_connection->write("bar foo", false));
  tcp_client->waitForData("bar foo");
  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
}

// Verify that CONNECT requests are treated the same way as upgrades by H/1 codec.
TEST_P(WebsocketIntegrationTest, BidirectionalConnectNoContentLengthNoTransferEncoding) {
  if (downstreamProtocol() != Http::CodecType::HTTP1 ||
      upstreamProtocol() != Http::CodecType::HTTP1) {
    return;
  }

  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_http2_protocol_options()->set_allow_connect(true);
        hcm.mutable_route_config()
            ->mutable_virtual_hosts(0)
            ->mutable_routes(0)
            ->mutable_match()
            ->mutable_connect_matcher();
        hcm.add_upgrade_configs()->set_upgrade_type("CONNECT");
      });
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));

  // Send upgrade request without CL and TE headers
  ASSERT_TRUE(tcp_client->write("CONNECT www.somewhere.com:80 HTTP/1.1\r\n\r\n", false, false));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT(fake_upstream_connection != nullptr);
  std::string received_data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("\r\n\r\n"), &received_data));
  // Make sure Envoy did not add TE or CL headers
  ASSERT_FALSE(absl::StrContains(received_data, "content-length"));
  ASSERT_FALSE(absl::StrContains(received_data, "transfer-encoding"));
  ASSERT_TRUE(fake_upstream_connection->write("HTTP/1.1 200 OK\r\n\r\n", false));

  tcp_client->waitForData("\r\n\r\n", false);
  // Make sure Envoy did not add TE or CL on the response path
  ASSERT_FALSE(absl::StrContains(tcp_client->data(), "content-length"));
  ASSERT_FALSE(absl::StrContains(tcp_client->data(), "transfer-encoding"));

  fake_upstream_connection->clearData();
  // Send data and make sure Envoy did not add chunk framing
  ASSERT_TRUE(tcp_client->write("foo bar", false, false));
  ASSERT_TRUE(fake_upstream_connection->waitForData(FakeRawConnection::waitForMatch("foo bar")));

  tcp_client->clearData();
  // Send response data and make sure Envoy did not add chunk framing on the response path
  ASSERT_TRUE(fake_upstream_connection->write("bar foo", false));
  tcp_client->waitForData("bar foo");
  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
}

// Test Websocket Upgrade in HTTP1 with 200 response code.
TEST_P(WebsocketIntegrationTest, Http1UpgradeStatusCodeOK) {
  if (downstreamProtocol() != Http::CodecType::HTTP1 ||
      upstreamProtocol() != Http::CodecType::HTTP1) {
    return;
  }
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.check_switch_protocol_websocket_handshake", "true"}});

  config_helper_.addConfigModifier(setRouteUsingWebsocket());
  initialize();

  auto in_correct_status_response_headers = upgradeResponseHeaders();
  in_correct_status_response_headers.setStatus(200);

  // The upgrade should be paused, but the response header is proxied back to downstream.
  performUpgrade(upgradeRequestHeaders(), in_correct_status_response_headers, true);
  EXPECT_EQ("200", response_->headers().Status()->value().getStringView());
  EXPECT_EQ("upgrade", response_->headers().Connection()->value().getStringView());
  EXPECT_EQ("websocket", response_->headers().Upgrade()->value().getStringView());

  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_destroy", 1);
  test_server_->waitForGaugeEq("http.config_test.downstream_cx_upgrades_active", 0);
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

// Test Websocket Upgrade with 200 response code from no HTTP1 upstream and downstream.
TEST_P(WebsocketIntegrationTest, NonHttp1UpgradeStatusCodeOK) {
  if (upstreamProtocol() == Http::CodecType::HTTP1 ||
      downstreamProtocol() == Http::CodecType::HTTP1) {
    return;
  }
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.check_switch_protocol_websocket_handshake", "true"}});

  config_helper_.addConfigModifier(setRouteUsingWebsocket());
  initialize();

  auto correct_status_response_headers = upgradeResponseHeaders();
  correct_status_response_headers.setStatus(200);
  performUpgrade(upgradeRequestHeaders(), correct_status_response_headers, true);

  // HTTP2 upstream response 200 is converted to 101.
  EXPECT_EQ("101", response_->headers().Status()->value().getStringView());
  test_server_->waitForGaugeEq("http.config_test.downstream_cx_upgrades_active", 1);
  codec_client_->close();
}

// Test Websocket Upgrade with 201 response code from no HTTP1 upstream and downstream.
// This patch will not impact no H/1 behaviors.
TEST_P(WebsocketIntegrationTest, NoHttp1UpstreamUpgradeStatus201) {
  if (upstreamProtocol() == Http::CodecType::HTTP1 ||
      downstreamProtocol() == Http::CodecType::HTTP1) {
    return;
  }
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.check_switch_protocol_websocket_handshake", "true"}});

  config_helper_.addConfigModifier(setRouteUsingWebsocket());
  initialize();

  auto correct_status_response_headers = upgradeResponseHeaders();
  correct_status_response_headers.setStatus(201);
  performUpgrade(upgradeRequestHeaders(), correct_status_response_headers, true);

  EXPECT_EQ("201", response_->headers().Status()->value().getStringView());
  test_server_->waitForGaugeEq("http.config_test.downstream_cx_upgrades_active", 1);
  codec_client_->close();
}

// Test Websocket Upgrade in HTTP1 with 426 response code.
// Upgrade is a HTTP1 header.
TEST_P(WebsocketIntegrationTest, Http1UpgradeStatusCodeUpgradeRequired) {
  if (downstreamProtocol() != Http::CodecType::HTTP1 ||
      upstreamProtocol() != Http::CodecType::HTTP1) {
    return;
  }
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.check_switch_protocol_websocket_handshake", "true"}});

  useAccessLog("%RESPONSE_CODE_DETAILS%");
  config_helper_.addConfigModifier(setRouteUsingWebsocket());
  initialize();

  auto in_correct_status_response_headers = upgradeResponseHeaders();
  in_correct_status_response_headers.setStatus(426);

  // The upgrade should be paused, but the response header is proxied back to downstream.
  performUpgrade(upgradeRequestHeaders(), in_correct_status_response_headers, true);
  EXPECT_EQ("426", response_->headers().Status()->value().getStringView());
  EXPECT_EQ("upgrade", response_->headers().Connection()->value().getStringView());
  EXPECT_EQ("websocket", response_->headers().Upgrade()->value().getStringView());

  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_destroy", 1);
  test_server_->waitForGaugeEq("http.config_test.downstream_cx_upgrades_active", 0);
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

// Test data flow when websocket handshake failed.
TEST_P(WebsocketIntegrationTest, BidirectionalUpgradeFailedWithPrePayload) {
  if (downstreamProtocol() != Http::CodecType::HTTP1 ||
      upstreamProtocol() != Http::CodecType::HTTP1) {
    return;
  }
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.check_switch_protocol_websocket_handshake", "true"}});

  config_helper_.addConfigModifier(setRouteUsingWebsocket());
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));

  // Send upgrade request with additional data.
  ASSERT_TRUE(tcp_client->write(
      "GET / HTTP/1.1\r\nHost: host\r\nconnection: upgrade\r\nupgrade: websocket\r\n\r\nfoo boo",
      false, false));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT(fake_upstream_connection != nullptr);
  std::string received_data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("\r\n\r\n"), &received_data));
  // Make sure Envoy did not add TE or CL headers
  ASSERT_FALSE(absl::StrContains(received_data, "content-length"));
  ASSERT_FALSE(absl::StrContains(received_data, "transfer-encoding"));
  ASSERT_TRUE(fake_upstream_connection->write(
      "HTTP/1.1 426 Upgrade Required\r\nconnection: upgrade\r\nupgrade: websocket\r\n\r\n", false));

  tcp_client->waitForData("\r\n\r\n", false);

  // Should not receive any data before handshake is finished.
  std::string received_data_prepayload;
  ASSERT_FALSE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("foo boo"), nullptr, std::chrono::milliseconds(10)));

  tcp_client->waitForDisconnect();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
}

TEST_P(WebsocketIntegrationTest, WebsocketUpgradeWithDisabledSmallBufferFilter) {
  if (downstreamProtocol() != Http::CodecType::HTTP1 ||
      upstreamProtocol() != Http::CodecType::HTTP1) {
    return;
  }

  const std::string WebsocketUpgradeWithDisabledSmallBufferConfig = R"EOF(
    name: typed-per-filter-config-for-ws
    virtual_hosts:
    - name: app-ws
      domains:
      - "*"
      routes:
      - match:
          prefix: "/websocket/test"
        route:
          cluster: cluster_0
          upgrade_configs:
          - upgrade_type: websocket
        typed_per_filter_config:
          buffer:
            "@type": type.googleapis.com/envoy.config.route.v3.FilterConfig
            disabled: true
)EOF";

  const std::string TinyBufferFilter = R"EOF(
    name: buffer
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer
        max_request_bytes : 1
)EOF";

  config_helper_.prependFilter(TinyBufferFilter);
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto route_config = hcm.mutable_route_config();
        TestUtility::loadFromYaml(WebsocketUpgradeWithDisabledSmallBufferConfig, *route_config);
      });
  initialize();

  performUpgrade(upgradeRequestHeaders(), upgradeResponseHeaders());
  sendBidirectionalData();

  // Send some final data from the client, and disconnect.
  codec_client_->sendData(*request_encoder_, "bye!", false);
  codec_client_->close();

  // Verify the final data was received and that the connection is torn down.
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, "hellobye!"));
  ASSERT_TRUE(waitForUpstreamDisconnectOrReset());
}
} // namespace Envoy
