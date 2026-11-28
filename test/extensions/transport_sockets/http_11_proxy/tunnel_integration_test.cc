#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/extensions/transport_sockets/http_11_proxy/v3/upstream_http_11_connect.pb.h"
#include "envoy/extensions/transport_sockets/raw_buffer/v3/raw_buffer.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

constexpr absl::string_view ProxyCredential = "Basic dXNlcjpwYXNzd29yZA==";

// End-to-end tests for the tunnel mode of the HTTP/1.1 proxy transport socket.
//
// The forward proxy is an internal listener of the Envoy under test. It terminates TLS, requires
// a Proxy-Authorization header on CONNECT requests, terminates the CONNECT, and forwards the
// tunneled bytes to the "origin" cluster (fake upstream 1). cluster_0 reaches the origin through
// that proxy, with TLS to the proxy and, optionally, TLS to the origin inside the tunnel.
class Http11TunnelIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                    public HttpIntegrationTest {
public:
  Http11TunnelIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    upstream_tls_ = true;
    // Fake upstream 0 is the endpoint of cluster_0 (only used as the CONNECT target), fake
    // upstream 1 is the endpoint of the "origin" cluster the proxy forwards the tunnel to.
    setUpstreamCount(2);
  }

  void initialize() override {
    if (upstream_tls_) {
      config_helper_.configureUpstreamTls();
    }
    config_helper_.addBootstrapExtension(R"EOF(
name: envoy.bootstrap.internal_listener
typed_config:
  "@type": type.googleapis.com/envoy.extensions.bootstrap.internal_listener.v3.InternalListener
)EOF");

    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* static_resources = bootstrap.mutable_static_resources();

      // The secret holding the credential sent to the proxy.
      auto* secret = static_resources->add_secrets();
      secret->set_name("proxy-credential");
      secret->mutable_generic_secret()->mutable_secret()->set_inline_string(client_credential_);

      // The origin cluster used by the proxy: plaintext TCP to fake upstream 1. Any TLS with the
      // origin runs end to end from cluster_0 through the tunnel.
      auto* cluster_0 = static_resources->mutable_clusters(0);
      auto* origin = static_resources->add_clusters();
      origin->CopyFrom(*cluster_0);
      origin->set_name("origin");
      origin->clear_transport_socket();
      origin->clear_typed_extension_protocol_options();

      // The forward proxy listener.
      TestUtility::loadFromYaml(proxyListenerConfig(), *static_resources->add_listeners());

      // Route cluster_0 through the proxy.
      envoy::config::core::v3::TransportSocket inner_socket;
      if (cluster_0->has_transport_socket()) {
        inner_socket.CopyFrom(cluster_0->transport_socket());
      } else {
        inner_socket.set_name("envoy.transport_sockets.raw_buffer");
        std::ignore = inner_socket.mutable_typed_config()->PackFrom(
            envoy::extensions::transport_sockets::raw_buffer::v3::RawBuffer());
      }
      envoy::extensions::transport_sockets::http_11_proxy::v3::Http11ProxyUpstreamTransport
          transport;
      *transport.mutable_transport_socket() = inner_socket;
      transport.mutable_default_proxy_address()
          ->mutable_envoy_internal_address()
          ->set_server_listener_name("proxy");
      TestUtility::loadFromYaml(proxyTransportSocketConfig(),
                                *transport.mutable_proxy_transport_socket());
      if (basic_auth_password_.has_value()) {
        transport.mutable_proxy_basic_auth()->set_username("user");
        transport.mutable_proxy_basic_auth()->mutable_password()->set_inline_string(
            *basic_auth_password_);
      } else {
        transport.mutable_proxy_authorization()->set_name("proxy-credential");
      }

      ConfigHelper::HttpProtocolOptions protocol_options;
      protocol_options.mutable_upstream_http_protocol_options()->set_auto_sni(true);
      protocol_options.mutable_explicit_http_config()->mutable_http_protocol_options();
      ConfigHelper::setProtocolOptions(*cluster_0, protocol_options);

      auto* transport_socket = cluster_0->mutable_transport_socket();
      transport_socket->set_name("envoy.transport_sockets.http_11_proxy");
      std::ignore = transport_socket->mutable_typed_config()->PackFrom(transport);
    });

    HttpIntegrationTest::initialize();
  }

  std::string proxyListenerConfig() const {
    return fmt::format(
        R"EOF(
name: proxy
internal_listener: {{}}
filter_chains:
- transport_socket:
    name: envoy.transport_sockets.tls
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext
      common_tls_context:
        tls_certificates:
        - certificate_chain: {{ filename: "{}" }}
          private_key: {{ filename: "{}" }}
  filters:
  - name: envoy.filters.network.http_connection_manager
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
      stat_prefix: proxy
      codec_type: HTTP1
      upgrade_configs:
      - upgrade_type: CONNECT
      http_filters:
      - name: envoy.filters.http.router
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
      route_config:
        virtual_hosts:
        - name: proxy
          domains: ["*"]
          routes:
          - match:
              connect_matcher: {{}}
              headers:
              - name: proxy-authorization
                string_match: {{ exact: "{}" }}
            route:
              cluster: origin
              upgrade_configs:
              - upgrade_type: CONNECT
                connect_config: {{}}
          - match:
              connect_matcher: {{}}
            direct_response:
              status: 407
)EOF",
        TestEnvironment::runfilesPath("test/config/integration/certs/servercert.pem"),
        TestEnvironment::runfilesPath("test/config/integration/certs/serverkey.pem"),
        ProxyCredential);
  }

  std::string proxyTransportSocketConfig() const {
    return fmt::format(R"EOF(
name: envoy.transport_sockets.tls
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext
  sni: proxy.lyft.com
  common_tls_context:
    validation_context:
      trusted_ca: {{ filename: "{}" }}
)EOF",
                       TestEnvironment::runfilesPath(proxy_trusted_ca_));
  }

  void testRequestThroughProxy() {
    initialize();
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = sendRequestAndWaitForResponse(
        default_request_headers_, 0, default_response_headers_, 0, /*upstream_index=*/1);
    EXPECT_EQ("200", response->headers().getStatusValue());
    // The credential is only sent to the proxy, never to the origin.
    EXPECT_TRUE(
        upstream_request_->headers().get(Http::LowerCaseString("proxy-authorization")).empty());
    EXPECT_EQ("/test/long/url", upstream_request_->headers().getPathValue());
    // The origin was reached through the proxy.
    test_server_->waitForCounter("http.proxy.downstream_rq_total", testing::Eq(1));
    test_server_->waitForCounter("cluster.origin.upstream_cx_total", testing::Eq(1));
  }

  std::string client_credential_{ProxyCredential};
  // If set, use Basic credentials with username "user" and this password instead of the secret.
  std::optional<std::string> basic_auth_password_;
  std::string proxy_trusted_ca_{"test/config/integration/certs/cacert.pem"};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, Http11TunnelIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// TLS to the proxy, Proxy-Authorization, and TLS to the origin through the tunnel.
TEST_P(Http11TunnelIntegrationTest, TlsProxyTlsOrigin) { testRequestThroughProxy(); }

// TLS to the proxy, Proxy-Authorization, and a plaintext origin: plaintext traffic is also
// tunneled with CONNECT, and requests are sent in origin-form.
TEST_P(Http11TunnelIntegrationTest, TlsProxyPlaintextOrigin) {
  upstream_tls_ = false;
  testRequestThroughProxy();
}

// Username and password credentials: "user:password" is the credential the proxy expects.
TEST_P(Http11TunnelIntegrationTest, BasicAuthCredentials) {
  basic_auth_password_ = "password";
  testRequestThroughProxy();
}

// The proxy rejects the CONNECT request when the Basic credentials are wrong.
TEST_P(Http11TunnelIntegrationTest, WrongBasicAuthCredentials) {
  basic_auth_password_ = "wrong";
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_EQ(0, test_server_->counter("cluster.origin.upstream_cx_total")->value());
}

// The proxy rejects the CONNECT request when the credential is wrong.
TEST_P(Http11TunnelIntegrationTest, WrongCredential) {
  client_credential_ = "Basic d3Jvbmc6Y3JlZGVudGlhbA==";
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  test_server_->waitForCounter("cluster.cluster_0.upstream_cx_connect_fail", testing::Ge(1));
  EXPECT_EQ(0, test_server_->counter("cluster.origin.upstream_cx_total")->value());
}

// The proxy certificate cannot be validated.
TEST_P(Http11TunnelIntegrationTest, UntrustedProxyCertificate) {
  proxy_trusted_ca_ = "test/config/integration/certs/upstreamcacert.pem";
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_EQ(0, test_server_->counter("http.proxy.downstream_rq_total")->value());
}

} // namespace
} // namespace Envoy
