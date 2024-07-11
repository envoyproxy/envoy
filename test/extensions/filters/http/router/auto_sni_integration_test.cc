#include <memory>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/upstream/upstream.h"

#include "source/common/tls/server_context_config_impl.h"
#include "source/common/tls/server_ssl_socket.h"

#include "test/integration/http_integration.h"

namespace Envoy {
namespace {
class AutoSniIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                               public Event::TestUsingSimulatedTime,
                               public HttpIntegrationTest {
public:
  AutoSniIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void setup(
      const std::function<void(envoy::config::core::v3::UpstreamHttpProtocolOptions*)>
          upstream_http_protocol_options_modifier =
              [](__attribute__((unused))
                 envoy::config::core::v3::UpstreamHttpProtocolOptions* options) {},
      const std::string& upstream_endpoint_hostname = "endpoint-hostname.lyft.com") {
    setUpstreamProtocol(Http::CodecType::HTTP1);

    config_helper_.addConfigModifier(
        [upstream_http_protocol_options_modifier,
         upstream_endpoint_hostname](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto& cluster_config = bootstrap.mutable_static_resources()->mutable_clusters()->at(0);
          ConfigHelper::HttpProtocolOptions protocol_options;
          protocol_options.mutable_upstream_http_protocol_options()->set_auto_sni(true);
          upstream_http_protocol_options_modifier(
              protocol_options.mutable_upstream_http_protocol_options());
          ConfigHelper::setProtocolOptions(
              *bootstrap.mutable_static_resources()->mutable_clusters(0), protocol_options);

          envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
          auto* validation_context =
              tls_context.mutable_common_tls_context()->mutable_validation_context();
          validation_context->mutable_trusted_ca()->set_filename(
              TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));
          cluster_config.mutable_transport_socket()->set_name("envoy.transport_sockets.tls");
          cluster_config.mutable_transport_socket()->mutable_typed_config()->PackFrom(tls_context);
          cluster_config.mutable_load_assignment()
              ->mutable_endpoints()
              ->at(0)
              .mutable_lb_endpoints()
              ->at(0)
              .mutable_endpoint()
              ->set_hostname(upstream_endpoint_hostname);
        });

    HttpIntegrationTest::initialize();
  }

  void createUpstreams() override {
    addFakeUpstream(createUpstreamSslContext(), Http::CodecType::HTTP1,
                    /*autonomous_upstream=*/false);
  }

  Network::DownstreamTransportSocketFactoryPtr createUpstreamSslContext() {
    envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
    auto* common_tls_context = tls_context.mutable_common_tls_context();
    auto* tls_cert = common_tls_context->add_tls_certificates();
    tls_cert->mutable_certificate_chain()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcert.pem"));
    tls_cert->mutable_private_key()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/upstreamkey.pem"));

    auto cfg = *Extensions::TransportSockets::Tls::ServerContextConfigImpl::create(
        tls_context, factory_context_);

    static auto* upstream_stats_store = new Stats::IsolatedStoreImpl();
    return *Extensions::TransportSockets::Tls::ServerSslSocketFactory::create(
        std::move(cfg), context_manager_, *upstream_stats_store->rootScope(),
        std::vector<std::string>{});
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

TEST_P(AutoSniIntegrationTest, AutoSniFromUpstreamTest) {
  setup([](envoy::config::core::v3::UpstreamHttpProtocolOptions* options) {
    options->set_auto_sni(false);
    options->set_auto_sni_from_upstream(true);
  });
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
  EXPECT_STREQ("endpoint-hostname.lyft.com",
               SSL_get_servername(ssl_socket->ssl(), TLSEXT_NAMETYPE_host_name));
}

TEST_P(AutoSniIntegrationTest, AutoSniFromUpstreamPrecedenceTest) {
  setup([](envoy::config::core::v3::UpstreamHttpProtocolOptions* options) {
    options->set_auto_sni(true);
    options->set_auto_sni_from_upstream(true);
  });
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
  EXPECT_STREQ("endpoint-hostname.lyft.com",
               SSL_get_servername(ssl_socket->ssl(), TLSEXT_NAMETYPE_host_name));
}

TEST_P(AutoSniIntegrationTest, AutoSniFromUpstreamAndAutoSanValidationFailureTest) {
  setup(
      [](envoy::config::core::v3::UpstreamHttpProtocolOptions* options) {
        options->set_auto_sni(false);
        options->set_auto_sni_from_upstream(true);
        options->set_auto_san_validation(true);
      },
      "not-a-san");
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const auto response_ = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "localhost"}});

  ASSERT_TRUE(response_->waitForEndStream());
  EXPECT_EQ("503", response_->headers().getStatusValue());

  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.ssl.fail_verify_san")->value());
}

TEST_P(AutoSniIntegrationTest, AutoSniFromUpstreamAndAutoSanValidationTest) {
  setup([](envoy::config::core::v3::UpstreamHttpProtocolOptions* options) {
    options->set_auto_sni(false);
    options->set_auto_sni_from_upstream(true);
    options->set_auto_san_validation(true);
  });
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
  EXPECT_STREQ("endpoint-hostname.lyft.com",
               SSL_get_servername(ssl_socket->ssl(), TLSEXT_NAMETYPE_host_name));
}

TEST_P(AutoSniIntegrationTest, AutoSniWithAltHeaderNameTest) {
  setup([](envoy::config::core::v3::UpstreamHttpProtocolOptions* options) {
    options->set_override_auto_sni_header("x-host");
  });
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const auto response_ =
      sendRequestAndWaitForResponse(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                                   {":path", "/"},
                                                                   {":scheme", "http"},
                                                                   {":authority", "localhost"},
                                                                   {"x-host", "custom"}},
                                    0, default_response_headers_, 0);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response_->complete());

  const Extensions::TransportSockets::Tls::SslHandshakerImpl* ssl_socket =
      dynamic_cast<const Extensions::TransportSockets::Tls::SslHandshakerImpl*>(
          fake_upstream_connection_->connection().ssl().get());
  EXPECT_STREQ("custom", SSL_get_servername(ssl_socket->ssl(), TLSEXT_NAMETYPE_host_name));
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
