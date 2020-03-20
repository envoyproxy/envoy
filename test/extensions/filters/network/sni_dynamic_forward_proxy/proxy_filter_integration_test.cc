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
  ProxyFilterIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam(),
                            ConfigHelper::TCP_PROXY_CONFIG) {}

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

    config_helper_.addListenerFilter("name: envoy.listener.tls_inspector");

    config_helper_.addConfigModifier([this, max_hosts](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Switch predefined cluster_0 to CDS filesystem sourcing.
      bootstrap.mutable_dynamic_resources()->mutable_cds_config()->set_path(cds_helper_.cds_path());
      bootstrap.mutable_static_resources()->clear_clusters();

      const std::string filter = fmt::format(R"EOF(
name: envoy.filters.http.dynamic_forward_proxy
typed_config:
  "@type": type.googleapis.com/envoy.config.filter.network.sni_dynamic_forward_proxy.v2alpha.FilterConfig
  dns_cache_config:
    name: foo
    dns_lookup_family: {}
    max_hosts: {}
  port_value: {}
)EOF",
                                             ipVersionToDnsFamily(GetParam()), max_hosts,
                                             fake_upstreams_[0]->localAddress()->ip()->port());
      config_helper_.addNetworkFilter(filter);
    });

    // Setup the initial CDS cluster.
    cluster_.mutable_connect_timeout()->CopyFrom(
        Protobuf::util::TimeUtil::MillisecondsToDuration(100));
    cluster_.set_name("cluster_0");
    cluster_.set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);

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

    TestUtility::loadFromYaml(cluster_type_config, *cluster_.mutable_cluster_type());

    // Load the CDS cluster and wait for it to initialize.
    cds_helper_.setCds({cluster_});
    HttpIntegrationTest::initialize();
    test_server_->waitForCounterEq("cluster_manager.cluster_added", 1);
    test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  }

  void createUpstreams() override {
    fake_upstreams_.emplace_back(new FakeUpstream(
        createUpstreamSslContext(), 0, FakeHttpConnection::Type::HTTP1, version_, timeSystem()));
  }

  // TODO(mattklein123): This logic is duplicated in various places. Cleanup in a follow up.
  Network::TransportSocketFactoryPtr createUpstreamSslContext() {
    envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
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

  Network::ClientConnectionPtr
  makeSslClientConnection(const Ssl::ClientSslTransportOptions& options) {

    Network::Address::InstanceConstSharedPtr address =
        Ssl::getSslAddress(version_, lookupPort("http"));
    auto client_transport_socket_factory_ptr =
        Ssl::createClientSslTransportSocketFactory(options, context_manager_, *api_);
    return dispatcher_->createClientConnection(
        address, Network::Address::InstanceConstSharedPtr(),
        client_transport_socket_factory_ptr->createTransportSocket({}), nullptr);
  }

  std::string upstream_cert_name_{"server"};
  CdsHelper cds_helper_;
  envoy::config::cluster::v3::Cluster cluster_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ProxyFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verify that upstream TLS works with auto verification for SAN as well as auto setting SNI.
TEST_P(ProxyFilterIntegrationTest, UpstreamTls) {
  setup();
  fake_upstreams_[0]->setReadDisableOnNewConnection(false);

  codec_client_ = makeHttpConnection(
      makeSslClientConnection(Ssl::ClientSslTransportOptions().setSni("localhost")));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(
      *dispatcher_, fake_upstream_connection_, TestUtility::DefaultTimeout, max_request_headers_kb_,
      max_request_headers_count_));

  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority",
       fmt::format("localhost:{}", fake_upstreams_[0]->localAddress()->ip()->port())}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();
  checkSimpleRequestSuccess(0, 0, response.get());
}
} // namespace
} // namespace Envoy
