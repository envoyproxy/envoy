#include "test/integration/http_integration.h"

#include "absl/strings/str_replace.h"
#include "gtest/gtest.h"

namespace Envoy {

// TODO(incfly):
// - upstream setup
//     src/test/integration/integration.cc:517 is the example of creating the fake upstream
//      with tls configuration, also see how Network::Test::createRawBufferSocket is invoked.
// - client envoy configuration modifying
class TransportSockeMatchIntegrationTest : public testing::TestWithParam<envoy::api::v2::Cluster_LbPolicy>,
                                    public HttpIntegrationTest {
public:
  TransportSockeMatchIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1,
                            TestEnvironment::getIpVersionsForTest().front(),
                            ConfigHelper::HTTP_PROXY_CONFIG),
        num_hosts_{4} {
    autonomous_upstream_ = true;
    setUpstreamCount(num_hosts_);

    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      auto* static_resources = bootstrap.mutable_static_resources();
      auto* cluster = static_resources->mutable_clusters(0);
      cluster->clear_hosts();
      // Create a load assignment with num_hosts_ entries with metadata split evenly between
      // type=a and type=b.
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
      }
    });

    config_helper_.addConfigModifier(
        [&](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager&
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
  }

  void configureRoute(envoy::api::v2::route::Route* route, const std::string& host_type) {
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

  void SetUp() override {
    setDownstreamProtocol(Http::CodecClient::Type::HTTP1);
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP1);
  }

  // Runs a subset lb test with the given request headers, expecting the x-host-type header to
  // the given type ("a" or "b"). If is_hash_lb_, verifies that a single host is selected over n
  // iterations (e.g. for maglev/hash-ring policies). Otherwise, expected more than one host to be
  // selected over n iterations.
  void runTest(Http::TestHeaderMapImpl& request_headers,
               const int n = 10) {
    std::set<std::string> hosts;
    for (int i = 0; i < n; i++) {
      Http::TestHeaderMapImpl response_headers{{":status", "200"}};

      // Send header only request.
      IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
      response->waitForEndStream();
    }

  }

  const uint32_t num_hosts_;

  const std::string hash_header_{"x-hash"};
  const std::string host_type_header_{"x-host-type"};
  const std::string host_header_{"x-host"};
  const std::string type_header_{"x-type"};
  const std::string type_key_{"type"};

  Http::TestHeaderMapImpl type_a_request_headers_{{":method", "GET"},  {":path", "/test"},
                                                  {":scheme", "http"}, {":authority", "host"},
                                                  {"x-type", "a"},     {"x-hash", "hash-a"}};
};

// Tests each subset-compatible load balancer policy with 4 hosts divided into 2 subsets.
TEST_F(TransportSockeMatchIntegrationTest, BasicMatch) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  runTest(type_a_request_headers_);
}

} // namespace Envoy
