#include "test/extensions/filters/http/common/compressor/websocket_with_compressor_integration_test.h"

#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "common/http/header_map_impl.h"
#include "common/protobuf/utility.h"

#include "test/integration/utility.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

Http::TestRequestHeaderMapImpl upgradeRequestHeaders(const char* upgrade_type = "websocket",
                                                     uint32_t content_length = 0) {
  return Http::TestRequestHeaderMapImpl{{":authority", "host"},
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

void WebsocketWithCompressorIntegrationTest::validateUpgradeRequestHeaders(
    const Http::RequestHeaderMap& original_proxied_request_headers,
    const Http::RequestHeaderMap& original_request_headers) {
  Http::TestRequestHeaderMapImpl proxied_request_headers(original_proxied_request_headers);
  if (proxied_request_headers.ForwardedProto()) {
    ASSERT_EQ(proxied_request_headers.getForwardedProtoValue(), "http");
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

void WebsocketWithCompressorIntegrationTest::validateUpgradeResponseHeaders(
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

INSTANTIATE_TEST_SUITE_P(Protocols, WebsocketWithCompressorIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

ConfigHelper::HttpModifierFunction setRouteUsingWebsocket() {
  return [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) { hcm.add_upgrade_configs()->set_upgrade_type("websocket"); };
}

void WebsocketWithCompressorIntegrationTest::initialize() {
  if (upstreamProtocol() != FakeHttpConnection::Type::HTTP1) {
    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          ConfigHelper::HttpProtocolOptions protocol_options;
          protocol_options.mutable_explicit_http_config()
              ->mutable_http2_protocol_options()
              ->set_allow_connect(true);
          ConfigHelper::setProtocolOptions(
              *bootstrap.mutable_static_resources()->mutable_clusters(0), protocol_options);
        });
  }
  if (downstreamProtocol() != Http::CodecClient::Type::HTTP1) {
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void { hcm.mutable_http2_protocol_options()->set_allow_connect(true); });
  }
  config_helper_.addFilter(R"EOF(
name: envoy.filters.http.compressor
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.compressor.v3.Compressor
  compressor_library:
    name: test
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.compression.gzip.compressor.v3.Gzip
)EOF");
  HttpProtocolIntegrationTest::initialize();
}

void WebsocketWithCompressorIntegrationTest::performUpgrade(
    const Http::TestRequestHeaderMapImpl& upgrade_request_headers,
    const Http::TestResponseHeaderMapImpl& upgrade_response_headers) {
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

void WebsocketWithCompressorIntegrationTest::sendBidirectionalData() {
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

// Technically not a websocket tests, but verifies normal upgrades have parity
// with websocket upgrades
TEST_P(WebsocketWithCompressorIntegrationTest, NonWebsocketUpgrade) {
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

TEST_P(WebsocketWithCompressorIntegrationTest, RouteSpecificUpgrade) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* foo_upgrade = hcm.add_upgrade_configs();
        foo_upgrade->set_upgrade_type("foo");
        foo_upgrade->mutable_enabled()->set_value(false);
      });
  auto host = config_helper_.createVirtualHost("host", "/websocket/test");
  host.mutable_routes(0)->mutable_route()->add_upgrade_configs()->set_upgrade_type("foo");
  config_helper_.addVirtualHost(host);
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

} // namespace Envoy
