#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/extensions/transport_sockets/internal_upstream/v3/internal_upstream.pb.h"
#include "envoy/network/connection.h"

#include "source/extensions/transport_sockets/internal_upstream/config.h"

#include "test/integration/http_integration.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/resources.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

class InternalUpstreamIntegrationTest : public testing::Test, public HttpIntegrationTest {
public:
  InternalUpstreamIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, TestEnvironment::getIpVersionsForTest().front(),
                            ConfigHelper::httpProxyConfig()) {
    setUpstreamCount(1);
  }

  void initialize() override {
    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* static_resources = bootstrap.mutable_static_resources();
      auto* cluster = static_resources->mutable_clusters()->Add();
      cluster->set_name("internal_upstream");
      // Insert internal upstream transport.
      TestUtility::loadFromYaml(R"EOF(
      name: envoy.transport_sockets.internal_upstream
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.internal_upstream.v3.InternalUpstreamTransport
        passthrough_metadata:
        - name: host_metadata
          kind: { host: {}}
        - name: cluster_metadata
          kind: { cluster: {}}
        passthrough_filter_state_objects:
        - name: internal_state
        transport_socket:
          name: envoy.transport_sockets.raw_buffer
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.transport_sockets.raw_buffer.v3.RawBuffer
      )EOF",
                                *cluster->mutable_transport_socket());
      // Insert internal address endpoint.
      cluster->clear_load_assignment();
      auto* load_assignment = cluster->mutable_load_assignment();
      load_assignment->set_cluster_name(cluster->name());
      auto* endpoints = load_assignment->add_endpoints();
      auto* lb_endpoint = endpoints->add_lb_endpoints();
      auto* endpoint = lb_endpoint->mutable_endpoint();
      auto* addr = endpoint->mutable_address()->mutable_envoy_internal_address();
      addr->set_server_listener_name("internal_address");
      if (add_metadata_) {
        auto* metadata = lb_endpoint->mutable_metadata();
        Config::Metadata::mutableMetadataValue(*metadata, "host_metadata", "value")
            .set_string_value("HOST");
        // Insert cluster metadata.
        Config::Metadata::mutableMetadataValue(*(cluster->mutable_metadata()), "cluster_metadata",
                                               "value")
            .set_string_value("CLUSTER");
      }
      // Emit metadata to access log.
      access_log_name_ = TestEnvironment::temporaryPath(TestUtility::uniqueFilename());

      // Insert internal listener.
      auto* listener = static_resources->mutable_listeners()->Add();
      TestUtility::loadFromYaml(fmt::format(R"EOF(
      name: internal_address
      address:
        envoy_internal_address:
          server_listener_name: internal_address
      filter_chains:
      - filters:
        - name: tcp_proxy
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
            cluster: cluster_0
            stat_prefix: internal_address
            access_log:
            - name: envoy.file_access_log
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
                path: {}
                log_format:
                  text_format_source:
                    inline_string: "%DYNAMIC_METADATA(host_metadata:value)%,%DYNAMIC_METADATA(cluster_metadata:value)%,%FILTER_STATE(internal_state:PLAIN)%\n"
      )EOF",
                                            access_log_name_),
                                *listener);
    });
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void {
          auto* route_config = hcm.mutable_route_config();
          auto* virtual_host = route_config->mutable_virtual_hosts(0);
          auto* route = virtual_host->mutable_routes(0);
          route->mutable_route()->set_cluster("internal_upstream");
        });
    config_helper_.addBootstrapExtension(R"EOF(
    name: envoy.bootstrap.internal_listener
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.bootstrap.internal_listener.v3.InternalListener
    )EOF");
    config_helper_.prependFilter(R"EOF(
    name: header-to-filter-state
    typed_config:
      "@type": type.googleapis.com/test.integration.filters.HeaderToFilterStateFilterConfig
      header_name: internal-header
      state_name: internal_state
      read_only: false
    )EOF");
    HttpIntegrationTest::initialize();
  }

  bool add_metadata_{true};
};

TEST_F(InternalUpstreamIntegrationTest, BasicFlow) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host.com"},
      {"internal-header", "FOO"},
  });
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  cleanupUpstreamAndDownstream();
  EXPECT_THAT(waitForAccessLog(access_log_name_), ::testing::HasSubstr("HOST,CLUSTER,FOO"));
}

TEST_F(InternalUpstreamIntegrationTest, BasicFlowMissing) {
  add_metadata_ = false;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host.com"},
  });
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  cleanupUpstreamAndDownstream();
  EXPECT_THAT(waitForAccessLog(access_log_name_), ::testing::HasSubstr("-,-,-"));
}

} // namespace
} // namespace Envoy
