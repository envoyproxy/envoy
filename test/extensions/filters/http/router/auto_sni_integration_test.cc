#include <memory>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/upstream/upstream.h"

#include "source/common/tls/server_context_config_impl.h"
#include "source/common/tls/server_ssl_socket.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace {
class AutoSniIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                               public Event::TestUsingSimulatedTime,
                               public HttpIntegrationTest {
public:
  AutoSniIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void setup(const std::string& override_auto_sni_header = "",
             const envoy::config::route::v3::RouteConfiguration* route_config = nullptr) {
    setUpstreamProtocol(Http::CodecType::HTTP1);

    config_helper_.addConfigModifier(
        [override_auto_sni_header](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto& cluster_config = bootstrap.mutable_static_resources()->mutable_clusters()->at(0);
          ConfigHelper::HttpProtocolOptions protocol_options;
          protocol_options.mutable_upstream_http_protocol_options()->set_auto_sni(true);
          if (!override_auto_sni_header.empty()) {
            protocol_options.mutable_upstream_http_protocol_options()->set_override_auto_sni_header(
                override_auto_sni_header);
          }
          ConfigHelper::setProtocolOptions(
              *bootstrap.mutable_static_resources()->mutable_clusters(0), protocol_options);

          envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
          auto* validation_context =
              tls_context.mutable_common_tls_context()->mutable_validation_context();
          validation_context->mutable_trusted_ca()->set_filename(
              TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));
          cluster_config.mutable_transport_socket()->set_name("envoy.transport_sockets.tls");
          cluster_config.mutable_transport_socket()->mutable_typed_config()->PackFrom(tls_context);
        });
    if (route_config != nullptr) {
      config_helper_.addConfigModifier(
          [route_config](envoy::extensions::filters::network::http_connection_manager::v3::
                             HttpConnectionManager& hcm) {
            auto* new_route_config = hcm.mutable_route_config();
            new_route_config->CopyFrom(*route_config);
          });
    }

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
        tls_context, factory_context_, false);

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

TEST_P(AutoSniIntegrationTest, AutoSniTestWithHostRewrite) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: local_service
  domains: ["*"]
  routes:
  - match:
      prefix: "/"
    name: "foo"
    route:
      cluster: cluster_0
      host_rewrite_literal: foo
  )EOF";
  envoy::config::route::v3::RouteConfiguration route_config;
  TestUtility::loadFromYaml(yaml, route_config);
  setup("", &route_config);

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
  EXPECT_STREQ("foo", SSL_get_servername(ssl_socket->ssl(), TLSEXT_NAMETYPE_host_name));
}

TEST_P(AutoSniIntegrationTest, AutoSniTestWithHostRewriteLegacy) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: local_service
  domains: ["*"]
  routes:
  - match:
      prefix: "/"
    name: "foo"
    route:
      cluster: cluster_0
      host_rewrite_literal: foo
  )EOF";
  envoy::config::route::v3::RouteConfiguration route_config;
  TestUtility::loadFromYaml(yaml, route_config);
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.use_route_host_mutation_for_auto_sni_san", "false");
  setup("", &route_config);
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
  EXPECT_STRNE("foo", SSL_get_servername(ssl_socket->ssl(), TLSEXT_NAMETYPE_host_name));
}

TEST_P(AutoSniIntegrationTest, AutoSniTestWithHostRewriteRegex) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: local_service
  domains: ["*"]
  routes:
  - match:
      prefix: "/"
    name: "foo"
    route:
      cluster: cluster_0
      host_rewrite_path_regex:
        pattern:
            regex: ".*"
        substitution: "foo"
  )EOF";
  envoy::config::route::v3::RouteConfiguration route_config;
  TestUtility::loadFromYaml(yaml, route_config);
  setup("", &route_config);

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
  EXPECT_STREQ("foo", SSL_get_servername(ssl_socket->ssl(), TLSEXT_NAMETYPE_host_name));
}

TEST_P(AutoSniIntegrationTest, AutoSniTestWithHostRewriteHeader) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: local_service
  domains: ["*"]
  routes:
  - match:
      prefix: "/"
    name: "foo"
    route:
      cluster: cluster_0
      host_rewrite_header: bar
  )EOF";
  envoy::config::route::v3::RouteConfiguration route_config;
  TestUtility::loadFromYaml(yaml, route_config);
  setup("", &route_config);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  const auto response_ =
      sendRequestAndWaitForResponse(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                                   {":path", "/"},
                                                                   {":scheme", "http"},
                                                                   {":authority", "localhost"},
                                                                   {"bar", "foo"}},
                                    0, default_response_headers_, 0);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response_->complete());

  const Extensions::TransportSockets::Tls::SslHandshakerImpl* ssl_socket =
      dynamic_cast<const Extensions::TransportSockets::Tls::SslHandshakerImpl*>(
          fake_upstream_connection_->connection().ssl().get());
  EXPECT_STREQ("foo", SSL_get_servername(ssl_socket->ssl(), TLSEXT_NAMETYPE_host_name));
}

TEST_P(AutoSniIntegrationTest, AutoSniWithAltHeaderNameTest) {
  setup("x-host");
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
