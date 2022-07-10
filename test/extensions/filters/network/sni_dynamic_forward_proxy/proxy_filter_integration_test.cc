#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "source/extensions/transport_sockets/tls/context_config_impl.h"
#include "source/extensions/transport_sockets/tls/ssl_socket.h"

#include "test/integration/http_integration.h"
#include "test/integration/ssl_utility.h"

namespace Envoy {
namespace {

class SniDynamicProxyFilterIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public Event::TestUsingSimulatedTime,
      public HttpIntegrationTest {
public:
  // This test is using HTTP integration test to use the utilities to pass SNI from downstream
  // to upstream. The config being tested is tcp_proxy.
  SniDynamicProxyFilterIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam(), ConfigHelper::tcpProxyConfig()) {}

  void setup(uint64_t max_hosts = 1024, uint32_t max_pending_requests = 1024) {
    setUpstreamProtocol(Http::CodecType::HTTP1);

    config_helper_.addListenerFilter(ConfigHelper::tlsInspectorFilter());

    config_helper_.addConfigModifier([this, max_hosts, max_pending_requests](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Switch predefined cluster_0 to CDS filesystem sourcing.
      bootstrap.mutable_dynamic_resources()->mutable_cds_config()->set_resource_api_version(
          envoy::config::core::v3::ApiVersion::V3);
      bootstrap.mutable_dynamic_resources()
          ->mutable_cds_config()
          ->mutable_path_config_source()
          ->set_path(cds_helper_.cds_path());
      bootstrap.mutable_static_resources()->clear_clusters();

      const std::string filter =
          fmt::format(R"EOF(
name: envoy.filters.http.dynamic_forward_proxy
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.sni_dynamic_forward_proxy.v3.FilterConfig
  dns_cache_config:
    name: foo
    dns_lookup_family: {}
    max_hosts: {}
    dns_cache_circuit_breaker:
      max_pending_requests: {}
  port_value: {}
)EOF",
                      Network::Test::ipVersionToDnsFamily(GetParam()), max_hosts,
                      max_pending_requests, fake_upstreams_[0]->localAddress()->ip()->port());
      config_helper_.addNetworkFilter(filter);
    });

    // Setup the initial CDS cluster.
    cluster_.mutable_connect_timeout()->CopyFrom(
        Protobuf::util::TimeUtil::MillisecondsToDuration(100));
    cluster_.set_name("cluster_0");
    cluster_.set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);

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

    // Load the CDS cluster and wait for it to initialize.
    cds_helper_.setCds({cluster_});
    HttpIntegrationTest::initialize();
    test_server_->waitForCounterEq("cluster_manager.cluster_added", 1);
    test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  }

  void createUpstreams() override {
    addFakeUpstream(
        Ssl::createFakeUpstreamSslContext(upstream_cert_name_, context_manager_, factory_context_),
        Http::CodecType::HTTP1, /*autonomous_upstream=*/false);
  }

  Network::ClientConnectionPtr
  makeSslClientConnection(const Ssl::ClientSslTransportOptions& options) {

    Network::Address::InstanceConstSharedPtr address =
        Ssl::getSslAddress(version_, lookupPort("http"));
    auto client_transport_socket_factory_ptr =
        Ssl::createClientSslTransportSocketFactory(options, context_manager_, *api_);
    return dispatcher_->createClientConnection(
        address, Network::Address::InstanceConstSharedPtr(),
        client_transport_socket_factory_ptr->createTransportSocket({}, nullptr), nullptr, nullptr);
  }

  std::string upstream_cert_name_{"server"};
  CdsHelper cds_helper_;
  envoy::config::cluster::v3::Cluster cluster_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, SniDynamicProxyFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verify that upstream TLS works with auto verification for SAN as well as auto setting SNI.
TEST_P(SniDynamicProxyFilterIntegrationTest, UpstreamTls) {
  setup();
  fake_upstreams_[0]->setReadDisableOnNewConnection(false);

  codec_client_ = makeHttpConnection(
      makeSslClientConnection(Ssl::ClientSslTransportOptions().setSni("localhost")));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));

  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority",
       fmt::format("localhost:{}", fake_upstreams_[0]->localAddress()->ip()->port())}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  checkSimpleRequestSuccess(0, 0, response.get());
}

TEST_P(SniDynamicProxyFilterIntegrationTest, CircuitBreakerInvokedUpstreamTls) {
  setup(1024, 0);

  codec_client_ = makeRawHttpConnection(
      makeSslClientConnection(Ssl::ClientSslTransportOptions().setSni("localhost")), absl::nullopt);
  ASSERT_FALSE(codec_client_->connected());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.dns_rq_pending_overflow")->value());
}

} // namespace
} // namespace Envoy
