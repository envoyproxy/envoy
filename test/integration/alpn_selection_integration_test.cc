#include "common/http/utility.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "extensions/transport_sockets/tls/context_config_impl.h"
#include "extensions/transport_sockets/tls/context_impl.h"
#include "extensions/transport_sockets/tls/ssl_socket.h"

#include "fake_upstream.h"
#include "test/integration/autonomous_upstream.h"
#include "test/integration/http_integration.h"

#include "absl/strings/str_replace.h"
#include "gtest/gtest.h"
#include <initializer_list>

namespace Envoy {

class AlpnSelectionIntegrationTest : public testing::Test, public HttpIntegrationTest {
public:
  AlpnSelectionIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1,
                            TestEnvironment::getIpVersionsForTest().front(),
                            ConfigHelper::httpProxyConfig()) {
  }

  void initialize() override {
    setDownstreamProtocol(Http::CodecClient::Type::HTTP1);
    setUpstreamProtocol(use_h2_ ? FakeHttpConnection::Type::HTTP2
                                : FakeHttpConnection::Type::HTTP1);
    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* static_resources = bootstrap.mutable_static_resources();
      auto* cluster = static_resources->mutable_clusters(0);

      if (use_h2_) {
          cluster->mutable_http2_protocol_options();
      }
      const std::string transport_socket_yaml = absl::StrFormat(
          R"EOF(
name: tls
typed_config:
  "@type": type.googleapis.com/envoy.api.v2.auth.UpstreamTlsContext
  common_tls_context:
    alpn_protocols: [ %s ]
    tls_certificates:
    - certificate_chain: { filename: "%s" }
      private_key: { filename: "%s" }
 )EOF",
          absl::StrJoin(configured_alpn_, ","),
          TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem"),
          TestEnvironment::runfilesPath("test/config/integration/certs/clientkey.pem"));
      auto* transport_socket = cluster->mutable_transport_socket();
      TestUtility::loadFromYaml(transport_socket_yaml, *transport_socket);
    });
    HttpIntegrationTest::initialize();
  }

  Network::TransportSocketFactoryPtr createUpstreamSslContext() {
    envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
    const std::string yaml = absl::StrFormat(
        R"EOF(
common_tls_context:
  alpn_protocols: [%s]
  tls_certificates:
  - certificate_chain: { filename: "%s" }
    private_key: { filename: "%s" }
  validation_context:
    trusted_ca: { filename: "%s" }
require_client_certificate: true
)EOF",
        absl::StrJoin(upstream_alpn_, ","),
        TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcert.pem"),
        TestEnvironment::runfilesPath("test/config/integration/certs/upstreamkey.pem"),
        TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
    TestUtility::loadFromYaml(yaml, tls_context);
    auto cfg = std::make_unique<Extensions::TransportSockets::Tls::ServerContextConfigImpl>(
        tls_context, factory_context_);
    static Stats::Scope* upstream_stats_store = new Stats::IsolatedStoreImpl();
    return std::make_unique<Extensions::TransportSockets::Tls::ServerSslSocketFactory>(
        std::move(cfg), context_manager_, *upstream_stats_store, std::vector<std::string>{});
  }

  void createUpstreams() override {
    auto endpoint = upstream_address_fn_(0);
    fake_upstreams_.emplace_back(new FakeUpstream(
        createUpstreamSslContext(), endpoint->ip()->port(),
        use_h2_ ? FakeHttpConnection::Type::HTTP2 : FakeHttpConnection::Type::HTTP1,
        endpoint->ip()->version(), timeSystem()));
  }

  bool use_h2_{};
  std::vector<std::string> upstream_alpn_;
  std::vector<std::string> configured_alpn_;
};

// No upstream ALPN is specified in the protocol, but we succesfully negotiate h2 ALPN
// due to the default ALPN set through the HTTP/2 conn pool.
TEST_F(AlpnSelectionIntegrationTest, Http2UpstreamMatchingAlpn) {
  use_h2_ = true;
  upstream_alpn_.emplace_back(Http::Utility::AlpnNames::get().Http2);
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();
  EXPECT_EQ(Http::Utility::AlpnNames::get().Http2, fake_upstream_connection_->connection().nextProtocol());

  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// No upstream ALPN is specified in the protocol and we fail to negotiate h2 ALPN
// since the upstream doesn't list h2 in its ALPN list. Note that the call still goes
// through because ALPN negotiation failure doesn't necessarily fail the call.
TEST_F(AlpnSelectionIntegrationTest, Http2UpstreamMismatchingAlpn) {
  use_h2_ = true;
  upstream_alpn_.emplace_back(Http::Utility::AlpnNames::get().Http11);
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();
  // No ALPN negotiated.
  EXPECT_EQ("", fake_upstream_connection_->connection().nextProtocol());

  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// The upstream supports h2,custom-alpn, and we configure the upstream TLS context to negotiate custom-alpn.
// No attempt to negoitate h2 should happen, so we should select custom-alpn.
TEST_F(AlpnSelectionIntegrationTest, Http2UpstreamConfiguredALPN) {
  use_h2_ = true;
  upstream_alpn_.emplace_back(Http::Utility::AlpnNames::get().Http2);
  upstream_alpn_.emplace_back("custom-alpn");
  configured_alpn_.emplace_back("custom-alpn");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();
  EXPECT_EQ("custom-alpn", fake_upstream_connection_->connection().nextProtocol());

  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// No upstream ALPN is specified in the protocol and we fail to negotiate h2 ALPN
// since the upstream doesn't list h2 in its ALPN list. Note that the call still goes
// through because ALPN negotiation failure doesn't necessarily fail the call.
TEST_F(AlpnSelectionIntegrationTest, Http11UpstreaMatchingAlpn) {
  upstream_alpn_.emplace_back(Http::Utility::AlpnNames::get().Http11);
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();
  EXPECT_EQ(Http::Utility::AlpnNames::get().Http11, fake_upstream_connection_->connection().nextProtocol());

  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// The upstream only lists h2 but we attempt to negotiate http/1.1 due to the default ALPN set by
// the conn pool. This results in no protocol being negotiated. Note that the call still goes
// through because ALPN negotiation failure doesn't necessarily fail the call.
TEST_F(AlpnSelectionIntegrationTest, Http11UpstreaMismatchingAlpn) {
  upstream_alpn_.emplace_back(Http::Utility::AlpnNames::get().Http2);
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();
  // No ALPN selected.
  EXPECT_EQ("", fake_upstream_connection_->connection().nextProtocol());

  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// The upstream supports http/1.1,custom-alpn, and we configure the upstream TLS context to negotiate custom-alpn.
// No attempt to negoitate http/1.1 should happen, so we should select custom-alpn.
TEST_F(AlpnSelectionIntegrationTest, Http11UpstreamConfiguredALPN) {
  upstream_alpn_.emplace_back(Http::Utility::AlpnNames::get().Http11);
  upstream_alpn_.emplace_back("custom-alpn");
  configured_alpn_.emplace_back("custom-alpn");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();
  EXPECT_EQ("custom-alpn", fake_upstream_connection_->connection().nextProtocol());

  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();
  EXPECT_EQ("200", response->headers().getStatusValue());
}
} // namespace Envoy
