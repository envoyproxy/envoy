#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/extensions/upstreams/http/v3/http_protocol_options.pb.h"

#include "source/common/config/metadata.h"

#include "test/integration/autonomous_upstream.h"
#include "test/integration/http_integration.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/resources.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "gtest/gtest.h"
#include "xds/type/matcher/v3/matcher.pb.h"

namespace Envoy {

class TransportSocketMatcherIntegrationTest : public testing::Test, public HttpIntegrationTest {
public:
  TransportSocketMatcherIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, TestEnvironment::getIpVersionsForTest().front(),
                            ConfigHelper::httpProxyConfig()) {
    autonomous_upstream_ = true;
    setUpstreamCount(num_hosts_);
  }

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* static_resources = bootstrap.mutable_static_resources();
      auto* cluster = static_resources->mutable_clusters(0);

      // Configure load balancing with subset.
      cluster->mutable_lb_subset_config()->add_subset_selectors()->add_keys(type_key_);

      // Setup transport socket matches.
      setupTransportSocketMatches(*cluster);

      // Configure the xDS-based matcher.
      setupTransportSocketMatcher(*cluster);

      // Setup endpoints.
      setupEndpoints(*cluster);
    });

    // Configure routes.
    config_helper_.addConfigModifier(
        [this](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          auto* vhost = hcm.mutable_route_config()->mutable_virtual_hosts(0);

          // Report upstream information in response headers.
          addResponseHeaders(*vhost);

          // Create routes for different test scenarios.
          vhost->clear_routes();
          configureRoute(vhost->add_routes(), "tls");
          configureRoute(vhost->add_routes(), "raw");
        });

    HttpIntegrationTest::initialize();
  }

  void setupTransportSocketMatches(envoy::config::cluster::v3::Cluster& cluster) {
    // TLS socket configuration.
    auto* tls_match = cluster.add_transport_socket_matches();
    tls_match->set_name("tls");
    auto* tls_socket = tls_match->mutable_transport_socket();
    tls_socket->set_name("tls");
    envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
    tls_context.mutable_common_tls_context()
        ->mutable_tls_certificates()
        ->Add()
        ->mutable_certificate_chain()
        ->set_filename(
            TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem"));
    tls_context.mutable_common_tls_context()
        ->mutable_tls_certificates(0)
        ->mutable_private_key()
        ->set_filename(
            TestEnvironment::runfilesPath("test/config/integration/certs/clientkey.pem"));
    tls_socket->mutable_typed_config()->PackFrom(tls_context);

    // Raw socket configuration.
    auto* raw_match = cluster.add_transport_socket_matches();
    raw_match->set_name("raw");
    raw_match->mutable_transport_socket()->set_name("raw_buffer");
  }

  void setupTransportSocketMatcher(envoy::config::cluster::v3::Cluster& cluster) {
    // Configure an xDS-based matcher using endpoint metadata.
    // Input: endpoint metadata key envoy.lb.
    xds::type::matcher::v3::Matcher matcher;
    const std::string matcher_yaml = R"EOF(
matcher_tree:
  input:
    name: envoy.matching.inputs.endpoint_metadata
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.transport_socket.v3.EndpointMetadataInput
      filter: envoy.lb
      path:
      - key: type
  exact_match_map:
    map:
      "tls":
        action:
          name: envoy.matching.action.transport_socket.name
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.transport_socket.v3.TransportSocketNameAction
            name: tls
      "raw":
        action:
          name: envoy.matching.action.transport_socket.name
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.transport_socket.v3.TransportSocketNameAction
            name: raw
)EOF";
    TestUtility::loadFromYaml(matcher_yaml, matcher);
    *cluster.mutable_transport_socket_matcher() = matcher;
  }

  void setupEndpoints(envoy::config::cluster::v3::Cluster& cluster) {
    cluster.clear_load_assignment();
    auto* load_assignment = cluster.mutable_load_assignment();
    load_assignment->set_cluster_name(cluster.name());
    auto* endpoints = load_assignment->add_endpoints();

    for (uint32_t i = 0; i < num_hosts_; i++) {
      auto* lb_endpoint = endpoints->add_lb_endpoints();
      auto* endpoint = lb_endpoint->mutable_endpoint();
      auto* addr = endpoint->mutable_address()->mutable_socket_address();
      addr->set_address(
          Network::Test::getLoopbackAddressString(TestEnvironment::getIpVersionsForTest().front()));
      addr->set_port_value(0);

      // Assign metadata for subset load balancing.
      auto* metadata = lb_endpoint->mutable_metadata();
      Envoy::Config::Metadata::mutableMetadataValue(*metadata, "envoy.lb", type_key_)
          .set_string_value((i % 2 == 0) ? "tls" : "raw");
    }
  }

  void addResponseHeaders(envoy::config::route::v3::VirtualHost& vhost) {
    // Report the host type in response.
    auto* resp_header = vhost.add_response_headers_to_add();
    auto* header = resp_header->mutable_header();
    header->set_key(host_type_header_);
    header->set_value(fmt::format(R"EOF(%UPSTREAM_METADATA(["envoy.lb", "{}"])%)EOF", type_key_));

    // Report the upstream remote address.
    resp_header = vhost.add_response_headers_to_add();
    header = resp_header->mutable_header();
    header->set_key(host_header_);
    header->set_value("%UPSTREAM_REMOTE_ADDRESS%");

    // Report protocol if available.
    resp_header = vhost.add_response_headers_to_add();
    header = resp_header->mutable_header();
    header->set_key("x-upstream-protocol");
    header->set_value("%PROTOCOL%");
  }

  void configureRoute(envoy::config::route::v3::Route* route, const std::string& route_type) {
    auto* match = route->mutable_match();
    match->set_prefix("/");

    // Match based on x-route-type header.
    auto* match_header = match->add_headers();
    match_header->set_name("x-route-type");
    match_header->mutable_string_match()->set_exact(route_type);

    // Route to cluster_0 with subset selection.
    auto* action = route->mutable_route();
    action->set_cluster("cluster_0");
    auto* metadata_match = action->mutable_metadata_match();
    Envoy::Config::Metadata::mutableMetadataValue(*metadata_match, "envoy.lb", type_key_)
        .set_string_value(route_type);
  }

  bool isTLSUpstream(int index) { return index % 2 == 0; }

  void createUpstreams() override {
    for (uint32_t i = 0; i < fake_upstreams_count_; ++i) {
      auto endpoint = upstream_address_fn_(i);
      if (isTLSUpstream(i)) {
        fake_upstreams_.emplace_back(new AutonomousUpstream(
            HttpIntegrationTest::createUpstreamTlsContext(upstreamConfig()), endpoint->ip()->port(),
            endpoint->ip()->version(), upstreamConfig(), false));
      } else {
        fake_upstreams_.emplace_back(new AutonomousUpstream(
            Network::Test::createRawBufferDownstreamSocketFactory(), endpoint->ip()->port(),
            endpoint->ip()->version(), upstreamConfig(), false));
      }
    }
  }

  void SetUp() override {
    setDownstreamProtocol(Http::CodecType::HTTP1);
    setUpstreamProtocol(Http::CodecType::HTTP1);
  }

  enum class MatchType { EndpointMetadata };

protected:
  const uint32_t num_hosts_{2};
  const std::string host_type_header_{"x-host-type"};
  const std::string host_header_{"x-host"};
  const std::string type_key_{"type"};
  bool use_matcher_{true};
  MatchType match_type_{MatchType::EndpointMetadata};

  Http::TestRequestHeaderMapImpl tls_request_headers_{{":method", "GET"},
                                                      {":path", "/test"},
                                                      {":scheme", "http"},
                                                      {":authority", "host"},
                                                      {"x-route-type", "tls"}};
  Http::TestRequestHeaderMapImpl raw_request_headers_{{":method", "GET"},
                                                      {":path", "/test"},
                                                      {":scheme", "http"},
                                                      {":authority", "host"},
                                                      {"x-route-type", "raw"}};
};

// Test the xDS-based transport socket matcher end-to-end using endpoint metadata input.
TEST_F(TransportSocketMatcherIntegrationTest, XdsMatcherEndpointMetadata) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Requests should route to the correct upstream sockets based on endpoint metadata type.
  for (int i = 0; i < 3; i++) {
    IntegrationStreamDecoderPtr response =
        codec_client_->makeHeaderOnlyRequest(tls_request_headers_);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());
    const auto header_entry = response->headers().get(Http::LowerCaseString{host_type_header_});
    EXPECT_FALSE(header_entry.empty());
    EXPECT_EQ("tls", header_entry[0]->value().getStringView());

    response = codec_client_->makeHeaderOnlyRequest(raw_request_headers_);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());
    const auto header_entry2 = response->headers().get(Http::LowerCaseString{host_type_header_});
    EXPECT_FALSE(header_entry2.empty());
    EXPECT_EQ("raw", header_entry2[0]->value().getStringView());
  }
}

// Simple smoke test to ensure multiple sequential requests work.
TEST_F(TransportSocketMatcherIntegrationTest, XdsMatcherSequentialSmoke) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  for (int i = 0; i < 2; i++) {
    IntegrationStreamDecoderPtr response =
        codec_client_->makeHeaderOnlyRequest(tls_request_headers_);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }
}

} // namespace Envoy
