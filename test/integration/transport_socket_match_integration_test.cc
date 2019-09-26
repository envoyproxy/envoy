#include "test/integration/http_integration.h"

#include "absl/strings/str_replace.h"
#include "gtest/gtest.h"

namespace Envoy {

// TODO(incfly):
// - upstream setup
//     src/test/integration/integration.cc:517 is the example of creating the fake upstream
//      with tls configuration, also see how Network::Test::createRawBufferSocket is invoked.
//    just creating the fake upstream right away here...inline code for now.
// - Client envoy configuration modifying
// bazel test //test/integration:transport_socket_match_integration_test --test_output=streamed   --test_arg='-l info'
class TransportSockeMatchIntegrationTest : public testing::Test, public HttpIntegrationTest {
public:
  TransportSockeMatchIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1,
                            TestEnvironment::getIpVersionsForTest().front(),
                            ConfigHelper::HTTP_PROXY_CONFIG),
        num_hosts_{4} {
    // xds_upstream is the control plane management server, not the upstream...
   // create_xds_upstream_ = true;
    // tls_xds_upstream_ = true;
    autonomous_upstream_ = true;
    setUpstreamCount(num_hosts_);

    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      auto* static_resources = bootstrap.mutable_static_resources();
      auto* cluster = static_resources->mutable_clusters(0);
      cluster->clear_hosts();
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
        //auto* metadata = lb_endpoint->mutable_metadata();
        //Envoy::Config::Metadata::mutableMetadataValue(*metadata, "envoy.lb", type_key_)
            //.set_string_value((i % 2 == 0) ? "a" : "b");
      }
    });
}

  void SetUp() override {
    setDownstreamProtocol(Http::CodecClient::Type::HTTP1);
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP1);
  }

  void runTest(Http::TestHeaderMapImpl& request_headers) {
    // Send header only request.
    IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
    response->waitForEndStream();
    EXPECT_EQ("200", response->headers().Status()->value().getStringView());

  }
  const uint32_t num_hosts_;
  const std::string host_type_header_{"x-host-type"};
  const std::string host_header_{"x-host"};
  const std::string type_key_{"type"};
  Http::TestHeaderMapImpl type_a_request_headers_{{":method", "GET"},  {":path", "/test"},
                                                  {":scheme", "http"}, {":authority", "host"},
                                                  {"x-type", "a"}};
};

TEST_F(TransportSockeMatchIntegrationTest, BasicMatch) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  runTest(type_a_request_headers_);
}

} // namespace Envoy
