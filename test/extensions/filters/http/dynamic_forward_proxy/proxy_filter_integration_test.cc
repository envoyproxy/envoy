#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "extensions/transport_sockets/tls/context_config_impl.h"
#include "extensions/transport_sockets/tls/ssl_socket.h"

#include "test/integration/http_integration.h"
#include "test/integration/ssl_utility.h"

namespace Envoy {
namespace {

class ProxyFilterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                   public Event::TestUsingSimulatedTime,
                                   public HttpIntegrationTest {
public:
  ProxyFilterIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void setup(uint64_t max_hosts = 1024, uint32_t max_pending_requests = 1024) {
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP1);

    const std::string filter = fmt::format(R"EOF(
name: dynamic_forward_proxy
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.dynamic_forward_proxy.v3.FilterConfig
  dns_cache_config:
    name: foo
    dns_lookup_family: {}
    max_hosts: {}
    dns_cache_circuit_breaker:
      max_pending_requests: {}
)EOF",
                                           Network::Test::ipVersionToDnsFamily(GetParam()),
                                           max_hosts, max_pending_requests);
    config_helper_.addFilter(filter);

    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Switch predefined cluster_0 to CDS filesystem sourcing.
      bootstrap.mutable_dynamic_resources()->mutable_cds_config()->set_path(cds_helper_.cds_path());
      bootstrap.mutable_static_resources()->clear_clusters();
    });

    // Set validate_clusters to false to allow us to reference a CDS cluster.
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) { hcm.mutable_route_config()->mutable_validate_clusters()->set_value(false); });

    // Setup the initial CDS cluster.
    cluster_.mutable_connect_timeout()->CopyFrom(
        Protobuf::util::TimeUtil::MillisecondsToDuration(100));
    cluster_.set_name("cluster_0");
    cluster_.set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);

    if (upstream_tls_) {
      envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
      auto* validation_context =
          tls_context.mutable_common_tls_context()->mutable_validation_context();
      validation_context->mutable_trusted_ca()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));
      cluster_.mutable_transport_socket()->set_name("envoy.transport_sockets.tls");
      cluster_.mutable_transport_socket()->mutable_typed_config()->PackFrom(tls_context);
    }

    const std::string cluster_type_config = fmt::format(
        R"EOF(
name: envoy.clusters.dynamic_forward_proxy
typed_config:
  "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
  dns_cache_config:
    name: foo
    dns_lookup_family: {}
    max_hosts: {}
    dns_cache_circuit_breaker:
      max_pending_requests: {}
)EOF",
        Network::Test::ipVersionToDnsFamily(GetParam()), max_hosts, max_pending_requests);

    TestUtility::loadFromYaml(cluster_type_config, *cluster_.mutable_cluster_type());
    cluster_.mutable_circuit_breakers()
        ->add_thresholds()
        ->mutable_max_pending_requests()
        ->set_value(max_pending_requests);

    // Load the CDS cluster and wait for it to initialize.
    cds_helper_.setCds({cluster_});
    HttpIntegrationTest::initialize();
    test_server_->waitForCounterEq("cluster_manager.cluster_added", 1);
    test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  }

  void createUpstreams() override {
    if (upstream_tls_) {
      addFakeUpstream(Ssl::createFakeUpstreamSslContext(upstream_cert_name_, context_manager_,
                                                        factory_context_),
                      FakeHttpConnection::Type::HTTP1);
    } else {
      HttpIntegrationTest::createUpstreams();
    }
  }

  void disableDnsCacheCircuitBreakers() {
    config_helper_.addRuntimeOverride("envoy.reloadable_features.enable_dns_cache_circuit_breakers",
                                      "false");
  }

  bool upstream_tls_{};
  std::string upstream_cert_name_{"upstreamlocalhost"};
  CdsHelper cds_helper_;
  envoy::config::cluster::v3::Cluster cluster_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ProxyFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// A basic test where we pause a request to lookup localhost, and then do another request which
// should hit the TLS cache.
TEST_P(ProxyFilterIntegrationTest, RequestWithBody) {
  setup();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestRequestHeaderMapImpl request_headers{
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

TEST_P(ProxyFilterIntegrationTest, RequestWithBodyWithClusterCircuitBreaker) {
  disableDnsCacheCircuitBreakers();
  setup();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestRequestHeaderMapImpl request_headers{
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

// Verify that after we populate the cache and reload the cluster we reattach to the cache with
// its existing hosts.
TEST_P(ProxyFilterIntegrationTest, ReloadClusterAndAttachToCache) {
  setup();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestRequestHeaderMapImpl request_headers{
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

  // Cause a cluster reload via CDS.
  cluster_.mutable_circuit_breakers()->add_thresholds()->mutable_max_connections()->set_value(100);
  cds_helper_.setCds({cluster_});
  test_server_->waitForCounterEq("cluster_manager.cluster_modified", 1);
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);

  // We need to wait until the workers have gotten the new cluster update. The only way we can
  // know this currently is when the connection pools drain and terminate.
  AssertionResult result = fake_upstream_connection_->waitForDisconnect();
  RELEASE_ASSERT(result, result.message());
  fake_upstream_connection_.reset();

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
  const Http::TestRequestHeaderMapImpl request_headers{
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
  simTime().advanceTimeWait(std::chrono::milliseconds(300001));
  test_server_->waitForGaugeEq("dns_cache.foo.num_hosts", 0);
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.host_removed")->value());
}

// Test DNS cache host overflow.
TEST_P(ProxyFilterIntegrationTest, DNSCacheHostOverflow) {
  setup(1);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority",
       fmt::format("localhost:{}", fake_upstreams_[0]->localAddress()->ip()->port())}};

  auto response =
      sendRequestAndWaitForResponse(request_headers, 1024, default_response_headers_, 1024);
  checkSimpleRequestSuccess(1024, 1024, response.get());

  // Send another request, this should lead to a response directly from the filter.
  const Http::TestRequestHeaderMapImpl request_headers2{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority", fmt::format("localhost2", fake_upstreams_[0]->localAddress()->ip()->port())}};
  response = codec_client_->makeHeaderOnlyRequest(request_headers2);
  response->waitForEndStream();
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.host_overflow")->value());
}

// Verify that upstream TLS works with auto verification for SAN as well as auto setting SNI.
TEST_P(ProxyFilterIntegrationTest, UpstreamTls) {
  upstream_tls_ = true;
  setup();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority",
       fmt::format("localhost:{}", fake_upstreams_[0]->localAddress()->ip()->port())}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest();

  const Extensions::TransportSockets::Tls::SslHandshakerImpl* ssl_socket =
      dynamic_cast<const Extensions::TransportSockets::Tls::SslHandshakerImpl*>(
          fake_upstream_connection_->connection().ssl().get());
  EXPECT_STREQ("localhost", SSL_get_servername(ssl_socket->ssl(), TLSEXT_NAMETYPE_host_name));

  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();
  checkSimpleRequestSuccess(0, 0, response.get());
}

TEST_P(ProxyFilterIntegrationTest, UpstreamTlsWithIpHost) {
  upstream_tls_ = true;
  setup();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority", fmt::format("{}:{}", Network::Test::getLoopbackAddressUrlString(GetParam()),
                                 fake_upstreams_[0]->localAddress()->ip()->port())}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest();

  // No SNI for IP hosts.
  const Extensions::TransportSockets::Tls::SslHandshakerImpl* ssl_socket =
      dynamic_cast<const Extensions::TransportSockets::Tls::SslHandshakerImpl*>(
          fake_upstream_connection_->connection().ssl().get());
  EXPECT_STREQ(nullptr, SSL_get_servername(ssl_socket->ssl(), TLSEXT_NAMETYPE_host_name));

  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();
  checkSimpleRequestSuccess(0, 0, response.get());
}

// Verify that auto-SAN verification fails with an incorrect certificate.
TEST_P(ProxyFilterIntegrationTest, UpstreamTlsInvalidSAN) {
  upstream_tls_ = true;
  upstream_cert_name_ = "upstream";
  setup();
  fake_upstreams_[0]->setReadDisableOnNewConnection(false);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority",
       fmt::format("localhost:{}", fake_upstreams_[0]->localAddress()->ip()->port())}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  response->waitForEndStream();
  EXPECT_EQ("503", response->headers().getStatusValue());

  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.ssl.fail_verify_san")->value());
}

TEST_P(ProxyFilterIntegrationTest, DnsCacheCircuitBreakersInvoked) {
  setup(1024, 0);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority",
       fmt::format("localhost:{}", fake_upstreams_[0]->localAddress()->ip()->port())}};

  auto response = codec_client_->makeRequestWithBody(request_headers, 1024);
  response->waitForEndStream();
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.dns_rq_pending_overflow")->value());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().Status()->value().getStringView());
}

TEST_P(ProxyFilterIntegrationTest, ClusterCircuitBreakersInvoked) {
  disableDnsCacheCircuitBreakers();
  setup(1024, 0);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority",
       fmt::format("localhost:{}", fake_upstreams_[0]->localAddress()->ip()->port())}};

  auto response = codec_client_->makeRequestWithBody(request_headers, 1024);
  response->waitForEndStream();
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_pending_overflow")->value());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().Status()->value().getStringView());
}

} // namespace
} // namespace Envoy
