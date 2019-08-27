#include "extensions/transport_sockets/tls/context_config_impl.h"
#include "extensions/transport_sockets/tls/ssl_socket.h"

#include "test/integration/http_integration.h"

namespace Envoy {
namespace {

class ProxyFilterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                   public Event::TestUsingSimulatedTime,
                                   public HttpIntegrationTest {
public:
  ProxyFilterIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  static std::string ipVersionToDnsFamily(Network::Address::IpVersion version) {
    switch (version) {
    case Network::Address::IpVersion::v4:
      return "V4_ONLY";
    case Network::Address::IpVersion::v6:
      return "V6_ONLY";
    }

    // This seems to be needed on the coverage build for some reason.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  void setup(uint64_t max_hosts = 1024) {
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP1);

    const std::string filter = fmt::format(R"EOF(
name: envoy.filters.http.dynamic_forward_proxy
config:
  dns_cache_config:
    name: foo
    dns_lookup_family: {}
    max_hosts: {}
)EOF",
                                           ipVersionToDnsFamily(GetParam()), max_hosts);
    config_helper_.addFilter(filter);

    config_helper_.addConfigModifier(
        [this, max_hosts](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
          auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters(0);
          cluster_0->clear_hosts();
          cluster_0->set_lb_policy(envoy::api::v2::Cluster::CLUSTER_PROVIDED);

          if (upstream_tls_) {
            auto context = cluster_0->mutable_tls_context();
            auto* validation_context =
                context->mutable_common_tls_context()->mutable_validation_context();
            validation_context->mutable_trusted_ca()->set_filename(
                TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));
          }

          const std::string cluster_type_config =
              fmt::format(R"EOF(
name: envoy.clusters.dynamic_forward_proxy
typed_config:
  "@type": type.googleapis.com/envoy.config.cluster.dynamic_forward_proxy.v2alpha.ClusterConfig
  dns_cache_config:
    name: foo
    dns_lookup_family: {}
    max_hosts: {}
)EOF",
                          ipVersionToDnsFamily(GetParam()), max_hosts);

          TestUtility::loadFromYaml(cluster_type_config, *cluster_0->mutable_cluster_type());
        });

    HttpIntegrationTest::initialize();
  }

  void createUpstreams() override {
    if (upstream_tls_) {
      fake_upstreams_.emplace_back(new FakeUpstream(
          createUpstreamSslContext(), 0, FakeHttpConnection::Type::HTTP1, version_, timeSystem()));
    } else {
      HttpIntegrationTest::createUpstreams();
    }
  }

  // TODO(mattklein123): This logic is duplicated in various places. Cleanup in a follow up.
  Network::TransportSocketFactoryPtr createUpstreamSslContext() {
    envoy::api::v2::auth::DownstreamTlsContext tls_context;
    auto* common_tls_context = tls_context.mutable_common_tls_context();
    auto* tls_cert = common_tls_context->add_tls_certificates();
    tls_cert->mutable_certificate_chain()->set_filename(TestEnvironment::runfilesPath(
        fmt::format("test/config/integration/certs/{}cert.pem", upstream_cert_name_)));
    tls_cert->mutable_private_key()->set_filename(TestEnvironment::runfilesPath(
        fmt::format("test/config/integration/certs/{}key.pem", upstream_cert_name_)));

    auto cfg = std::make_unique<Extensions::TransportSockets::Tls::ServerContextConfigImpl>(
        tls_context, factory_context_);

    static Stats::Scope* upstream_stats_store = new Stats::IsolatedStoreImpl();
    return std::make_unique<Extensions::TransportSockets::Tls::ServerSslSocketFactory>(
        std::move(cfg), context_manager_, *upstream_stats_store, std::vector<std::string>{});
  }

  bool upstream_tls_{};
  std::string upstream_cert_name_{"upstreamlocalhost"};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ProxyFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// A basic test where we pause a request to lookup localhost, and then do another request which
// should hit the TLS cache.
TEST_P(ProxyFilterIntegrationTest, RequestWithBody) {
  setup();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestHeaderMapImpl request_headers{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority",
       fmt::format("localhost:{}", fake_upstreams_[0]->localAddress()->ip()->port())}};

  auto response =
      sendRequestAndWaitForResponse(request_headers, 1024, default_response_headers_, 1024);
  checkSimpleRequestSuccess(1024, 1024, response.get());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.dns_query_attempt")->value());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.host_added")->value());

  // Now send another request. This should hit the DNS cache.
  response = sendRequestAndWaitForResponse(request_headers, 512, default_response_headers_, 512);
  checkSimpleRequestSuccess(512, 512, response.get());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.dns_query_attempt")->value());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.host_added")->value());
}

// Verify that we expire hosts.
TEST_P(ProxyFilterIntegrationTest, RemoveHostViaTTL) {
  setup();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestHeaderMapImpl request_headers{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority",
       fmt::format("localhost:{}", fake_upstreams_[0]->localAddress()->ip()->port())}};

  auto response =
      sendRequestAndWaitForResponse(request_headers, 1024, default_response_headers_, 1024);
  checkSimpleRequestSuccess(1024, 1024, response.get());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.dns_query_attempt")->value());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.host_added")->value());
  EXPECT_EQ(1, test_server_->gauge("dns_cache.foo.num_hosts")->value());
  cleanupUpstreamAndDownstream();

  // > 5m
  simTime().sleep(std::chrono::milliseconds(300001));
  test_server_->waitForGaugeEq("dns_cache.foo.num_hosts", 0);
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.host_removed")->value());
}

// Test DNS cache host overflow.
TEST_P(ProxyFilterIntegrationTest, DNSCacheHostOverflow) {
  setup(1);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestHeaderMapImpl request_headers{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority",
       fmt::format("localhost:{}", fake_upstreams_[0]->localAddress()->ip()->port())}};

  auto response =
      sendRequestAndWaitForResponse(request_headers, 1024, default_response_headers_, 1024);
  checkSimpleRequestSuccess(1024, 1024, response.get());

  // Send another request, this should lead to a response directly from the filter.
  const Http::TestHeaderMapImpl request_headers2{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority", fmt::format("localhost2", fake_upstreams_[0]->localAddress()->ip()->port())}};
  response = codec_client_->makeHeaderOnlyRequest(request_headers2);
  response->waitForEndStream();
  EXPECT_EQ("503", response->headers().Status()->value().getStringView());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.host_overflow")->value());
}

// Verify that upstream TLS works with auto verification for SAN as well as auto setting SNI.
TEST_P(ProxyFilterIntegrationTest, UpstreamTls) {
  upstream_tls_ = true;
  setup();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestHeaderMapImpl request_headers{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority",
       fmt::format("localhost:{}", fake_upstreams_[0]->localAddress()->ip()->port())}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest();

  const Extensions::TransportSockets::Tls::SslSocket* ssl_socket =
      dynamic_cast<const Extensions::TransportSockets::Tls::SslSocket*>(
          fake_upstream_connection_->connection().ssl());
  EXPECT_STREQ("localhost",
               SSL_get_servername(ssl_socket->rawSslForTest(), TLSEXT_NAMETYPE_host_name));

  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();
  checkSimpleRequestSuccess(0, 0, response.get());
}

TEST_P(ProxyFilterIntegrationTest, UpstreamTlsWithIpHost) {
  upstream_tls_ = true;
  setup();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestHeaderMapImpl request_headers{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority", fmt::format("{}:{}", Network::Test::getLoopbackAddressUrlString(GetParam()),
                                 fake_upstreams_[0]->localAddress()->ip()->port())}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest();

  // No SNI for IP hosts.
  const Extensions::TransportSockets::Tls::SslSocket* ssl_socket =
      dynamic_cast<const Extensions::TransportSockets::Tls::SslSocket*>(
          fake_upstream_connection_->connection().ssl());
  EXPECT_STREQ(nullptr, SSL_get_servername(ssl_socket->rawSslForTest(), TLSEXT_NAMETYPE_host_name));

  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();
  checkSimpleRequestSuccess(0, 0, response.get());
}

// Verify that auto-SAN verification fails with an incorrect certificate.
TEST_P(ProxyFilterIntegrationTest, UpstreamTlsInvalidSAN) {
  upstream_tls_ = true;
  upstream_cert_name_ = "upstream";
  setup();
  // The upstream connection is going to fail handshake so make sure it can read and we expect
  // it to disconnect.
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  fake_upstreams_[0]->setReadDisableOnNewConnection(false);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestHeaderMapImpl request_headers{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority",
       fmt::format("localhost:{}", fake_upstreams_[0]->localAddress()->ip()->port())}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  response->waitForEndStream();
  EXPECT_EQ("503", response->headers().Status()->value().getStringView());

  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.ssl.fail_verify_san")->value());
}

} // namespace
} // namespace Envoy
