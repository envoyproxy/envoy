#include <memory>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/upstream/upstream.h"

#include "extensions/transport_sockets/tls/context_config_impl.h"
#include "extensions/transport_sockets/tls/ssl_socket.h"

#include "test/integration/http_integration.h"

namespace Envoy {
namespace {
class AutoSniIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                               public Event::TestUsingSimulatedTime,
                               public HttpIntegrationTest {
public:
  AutoSniIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void setup() {
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP1);

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto& cluster_config = bootstrap.mutable_static_resources()->mutable_clusters()->at(0);
      cluster_config.mutable_upstream_http_protocol_options()->set_auto_sni(true);

      envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
      auto* validation_context =
          tls_context.mutable_common_tls_context()->mutable_validation_context();
      validation_context->mutable_trusted_ca()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));
      cluster_config.mutable_transport_socket()->set_name("envoy.transport_sockets.tls");
      cluster_config.mutable_transport_socket()->mutable_typed_config()->PackFrom(tls_context);
    });

    HttpIntegrationTest::initialize();
  }

  void createUpstreams() override {
    addFakeUpstream(createUpstreamSslContext(), FakeHttpConnection::Type::HTTP1);
  }

  Network::TransportSocketFactoryPtr createUpstreamSslContext() {
    envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
    auto* common_tls_context = tls_context.mutable_common_tls_context();
    auto* tls_cert = common_tls_context->add_tls_certificates();
    tls_cert->mutable_certificate_chain()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcert.pem"));
    tls_cert->mutable_private_key()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/upstreamkey.pem"));

    auto cfg = std::make_unique<Extensions::TransportSockets::Tls::ServerContextConfigImpl>(
        tls_context, factory_context_);

    static Stats::Scope* upstream_stats_store = new Stats::IsolatedStoreImpl();
    return std::make_unique<Extensions::TransportSockets::Tls::ServerSslSocketFactory>(
        std::move(cfg), context_manager_, *upstream_stats_store, std::vector<std::string>{});
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AutoSniIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AutoSniIntegrationTest, BasicAutoSniTest) {
  setup();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const auto response_ = sendRequestAndWaitForResponse(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "localhost"}},
      0, default_response_headers_, 0);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response_->complete());

  const Extensions::TransportSockets::Tls::SslHandshakerImpl* ssl_socket =
      dynamic_cast<const Extensions::TransportSockets::Tls::SslHandshakerImpl*>(
          fake_upstream_connection_->connection().ssl().get());
  EXPECT_STREQ("localhost", SSL_get_servername(ssl_socket->ssl(), TLSEXT_NAMETYPE_host_name));
}

TEST_P(AutoSniIntegrationTest, PassingNotDNS) {
  setup();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const auto response_ = sendRequestAndWaitForResponse(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "127.0.0.1"}},
      0, default_response_headers_, 0);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response_->complete());

  const Extensions::TransportSockets::Tls::SslHandshakerImpl* ssl_socket =
      dynamic_cast<const Extensions::TransportSockets::Tls::SslHandshakerImpl*>(
          fake_upstream_connection_->connection().ssl().get());
  EXPECT_STREQ(nullptr, SSL_get_servername(ssl_socket->ssl(), TLSEXT_NAMETYPE_host_name));
}

TEST_P(AutoSniIntegrationTest, PassingHostWithoutPort) {
  setup();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const auto response_ = sendRequestAndWaitForResponse(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "example.com:8080"}},
      0, default_response_headers_, 0);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response_->complete());

  const Extensions::TransportSockets::Tls::SslHandshakerImpl* ssl_socket =
      dynamic_cast<const Extensions::TransportSockets::Tls::SslHandshakerImpl*>(
          fake_upstream_connection_->connection().ssl().get());
  EXPECT_STREQ("example.com", SSL_get_servername(ssl_socket->ssl(), TLSEXT_NAMETYPE_host_name));
}

} // namespace
} // namespace Envoy
