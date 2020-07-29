#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "extensions/transport_sockets/tls/context_config_impl.h"
#include "extensions/transport_sockets/tls/context_impl.h"
#include "extensions/transport_sockets/tls/ssl_socket.h"

#include "test/integration/autonomous_upstream.h"
#include "test/integration/http_integration.h"

#include "absl/strings/str_replace.h"
#include "gtest/gtest.h"

namespace Envoy {

class TransportSockeMatchIntegrationTest : public testing::Test, public HttpIntegrationTest {
public:
  TransportSockeMatchIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1,
                            TestEnvironment::getIpVersionsForTest().front(),
                            ConfigHelper::httpProxyConfig()),
        num_hosts_{2} {
    autonomous_upstream_ = true;
    setUpstreamCount(num_hosts_);
  }

  void initialize() override {
    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* static_resources = bootstrap.mutable_static_resources();
      auto* cluster = static_resources->mutable_clusters(0);
      cluster->mutable_lb_subset_config()->add_subset_selectors()->add_keys(type_key_);
      if (enable_transport_socket_match_) {
        const std::string match_yaml = absl::StrFormat(
            R"EOF(
name: "tls_socket"
match:
  mtlsReady: "true"
transport_socket:
  name: tls
  typed_config:
    "@type": type.googleapis.com/envoy.api.v2.auth.UpstreamTlsContext
    common_tls_context:
      tls_certificates:
      - certificate_chain: { filename: "%s" }
        private_key: { filename: "%s" }
 )EOF",
            TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem"),
            TestEnvironment::runfilesPath("test/config/integration/certs/clientkey.pem"));
        auto* transport_socket_match = cluster->add_transport_socket_matches();
        TestUtility::loadFromYaml(match_yaml, *transport_socket_match);
      }
      // Setup the client Envoy TLS config.
      cluster->clear_load_assignment();
      auto* load_assignment = cluster->mutable_load_assignment();
      load_assignment->set_cluster_name(cluster->name());
      auto* endpoints = load_assignment->add_endpoints();
      for (uint32_t i = 0; i < num_hosts_; i++) {
        auto* lb_endpoint = endpoints->add_lb_endpoints();
        // ConfigHelper will fill in ports later.
        auto* endpoint = lb_endpoint->mutable_endpoint();
        auto* addr = endpoint->mutable_address()->mutable_socket_address();
        addr->set_address(Network::Test::getLoopbackAddressString(
            TestEnvironment::getIpVersionsForTest().front()));
        addr->set_port_value(0);
        // Assign type metadata based on i.
        auto* metadata = lb_endpoint->mutable_metadata();
        Envoy::Config::Metadata::mutableMetadataValue(*metadata, "envoy.lb", type_key_)
            .set_string_value((i % 2 == 0) ? "a" : "b");
        if (isTLSUpstream(i)) {
          Envoy::Config::Metadata::mutableMetadataValue(
              *metadata, Envoy::Config::MetadataFilters::get().ENVOY_TRANSPORT_SOCKET_MATCH,
              "mtlsReady")
              .set_string_value("true");
        }
      }
    });
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          auto* vhost = hcm.mutable_route_config()->mutable_virtual_hosts(0);

          // Report the host's type metadata and remote address on every response.
          auto* resp_header = vhost->add_response_headers_to_add();
          auto* header = resp_header->mutable_header();
          header->set_key(host_type_header_);
          header->set_value(
              fmt::format(R"EOF(%UPSTREAM_METADATA(["envoy.lb", "{}"])%)EOF", type_key_));

          resp_header = vhost->add_response_headers_to_add();
          header = resp_header->mutable_header();
          header->set_key(host_header_);
          header->set_value("%UPSTREAM_REMOTE_ADDRESS%");

          // Create routes for x-type=a and x-type=b headers.
          vhost->clear_routes();
          configureRoute(vhost->add_routes(), "a");
          configureRoute(vhost->add_routes(), "b");
        });
    HttpIntegrationTest::initialize();
  }

  void configureRoute(envoy::config::route::v3::Route* route, const std::string& host_type) {
    auto* match = route->mutable_match();
    match->set_prefix("/");

    // Match the x-type header against the given host_type (a/b).
    auto* match_header = match->add_headers();
    match_header->set_name(type_header_);
    match_header->set_exact_match(host_type);

    // Route to cluster_0, selecting metadata type=a or type=b.
    auto* action = route->mutable_route();
    action->set_cluster("cluster_0");
    auto* metadata_match = action->mutable_metadata_match();
    Envoy::Config::Metadata::mutableMetadataValue(*metadata_match, "envoy.lb", type_key_)
        .set_string_value(host_type);
  };

  Network::TransportSocketFactoryPtr createUpstreamSslContext() {
    envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
    const std::string yaml = absl::StrFormat(
        R"EOF(
common_tls_context:
  tls_certificates:
  - certificate_chain: { filename: "%s" }
    private_key: { filename: "%s" }
  validation_context:
    trusted_ca: { filename: "%s" }
require_client_certificate: true
)EOF",
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

  bool isTLSUpstream(int index) { return index % 2 == 0; }

  void createUpstreams() override {
    for (uint32_t i = 0; i < fake_upstreams_count_; ++i) {
      auto endpoint = upstream_address_fn_(i);
      if (isTLSUpstream(i)) {
        fake_upstreams_.emplace_back(new AutonomousUpstream(
            createUpstreamSslContext(), endpoint->ip()->port(), FakeHttpConnection::Type::HTTP1,
            endpoint->ip()->version(), timeSystem(), false));
      } else {
        fake_upstreams_.emplace_back(new AutonomousUpstream(
            Network::Test::createRawBufferSocketFactory(), endpoint->ip()->port(),
            FakeHttpConnection::Type::HTTP1, endpoint->ip()->version(), timeSystem(), false));
      }
    }
  }

  void SetUp() override {
    setDownstreamProtocol(Http::CodecClient::Type::HTTP1);
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP1);
  }

  const uint32_t num_hosts_;
  Http::TestRequestHeaderMapImpl type_a_request_headers_{{":method", "GET"},
                                                         {":path", "/test"},
                                                         {":scheme", "http"},
                                                         {":authority", "host"},
                                                         {"x-type", "a"}};
  Http::TestRequestHeaderMapImpl type_b_request_headers_{{":method", "GET"},
                                                         {":path", "/test"},
                                                         {":scheme", "http"},
                                                         {":authority", "host"},
                                                         {"x-type", "b"}};
  const std::string host_type_header_{"x-host-type"};
  const std::string host_header_{"x-host"};
  const std::string type_header_{"x-type"};
  const std::string type_key_{"type"};
  bool enable_transport_socket_match_{true};
};

TEST_F(TransportSockeMatchIntegrationTest, TlsAndPlaintextSucceed) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  for (int i = 0; i < 3; i++) {
    IntegrationStreamDecoderPtr response =
        codec_client_->makeHeaderOnlyRequest(type_a_request_headers_);
    response->waitForEndStream();
    EXPECT_EQ("200", response->headers().getStatusValue());
    response = codec_client_->makeHeaderOnlyRequest(type_b_request_headers_);
    response->waitForEndStream();
    EXPECT_EQ("200", response->headers().getStatusValue());
  }
}

TEST_F(TransportSockeMatchIntegrationTest, TlsAndPlaintextFailsWithoutSocketMatch) {
  enable_transport_socket_match_ = false;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  for (int i = 0; i < 3; i++) {
    IntegrationStreamDecoderPtr response =
        codec_client_->makeHeaderOnlyRequest(type_a_request_headers_);
    response->waitForEndStream();
    EXPECT_EQ("503", response->headers().getStatusValue());
    response = codec_client_->makeHeaderOnlyRequest(type_b_request_headers_);
    response->waitForEndStream();
    EXPECT_EQ("200", response->headers().getStatusValue());
  }
}
} // namespace Envoy
