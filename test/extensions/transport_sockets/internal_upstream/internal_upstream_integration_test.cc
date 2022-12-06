#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/bootstrap/internal_listener/v3/internal_listener.pb.h"
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

  void setupBootstrapExtension(envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    envoy::extensions::bootstrap::internal_listener::v3::InternalListener config;
    if (buffer_size_specified_) {
      config.mutable_buffer_size_kb()->set_value(buffer_size_);
    }
    auto* boostrap_extension = bootstrap.add_bootstrap_extensions();
    boostrap_extension->mutable_typed_config()->PackFrom(config);
    boostrap_extension->set_name("envoy.bootstrap.internal_listener");
  }
  void initialize() override {
    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      setupBootstrapExtension(bootstrap);
      auto* static_resources = bootstrap.mutable_static_resources();
      auto* cluster = static_resources->mutable_clusters()->Add();
      cluster->set_name("internal_upstream");
      // Insert internal upstream transport.
      if (use_transport_socket_) {
        TestUtility::loadFromYaml(R"EOF(
        name: envoy.transport_sockets.internal_upstream
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.internal_upstream.v3.InternalUpstreamTransport
          passthrough_metadata:
          - name: host_metadata
            kind: { host: {}}
          - name: cluster_metadata
            kind: { cluster: {}}
          transport_socket:
            name: envoy.transport_sockets.raw_buffer
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.transport_sockets.raw_buffer.v3.RawBuffer
        )EOF",
                                  *cluster->mutable_transport_socket());
      }
      // Insert internal address endpoint.
      cluster->clear_load_assignment();
      auto* load_assignment = cluster->mutable_load_assignment();
      load_assignment->set_cluster_name(cluster->name());
      auto* endpoints = load_assignment->add_endpoints();
      auto* lb_endpoint = endpoints->add_lb_endpoints();
      auto* endpoint = lb_endpoint->mutable_endpoint();
      auto* addr = endpoint->mutable_address()->mutable_envoy_internal_address();
      addr->set_server_listener_name("internal_address");
      addr->set_endpoint_id("lorem_ipsum");
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
      internal_listener: {{}}
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
    config_helper_.prependFilter(R"EOF(
    name: header-to-filter-state
    typed_config:
      "@type": type.googleapis.com/test.integration.filters.HeaderToFilterStateFilterConfig
      header_name: internal-header
      state_name: internal_state
      shared: true
    )EOF");
    HttpIntegrationTest::initialize();
  }

  // Send bidirectional data through the internal connection.
  void internalConnectionBufferSizeTest() {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    // Send HTTP request with 10 KiB payload.
    auto response = codec_client_->makeRequestWithBody(default_request_headers_, 10240);
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    // Send HTTP response with 20 KiB payload.
    upstream_request_->encodeData(20480, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_EQ(10240U, upstream_request_->bodyLength());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
    EXPECT_EQ(20480U, response->body().size());
    cleanupUpstreamAndDownstream();
  }

  Envoy::Http::TestRequestHeaderMapImpl request_header_{Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host.com"},
      {"internal-header", "FOO"},
  }};
  bool add_metadata_{true};
  bool use_transport_socket_{true};
  bool buffer_size_specified_{false};
  uint32_t buffer_size_{0};
};

TEST_F(InternalUpstreamIntegrationTest, BasicFlow) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(request_header_);
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

TEST_F(InternalUpstreamIntegrationTest, BasicFlowWithoutTransportSocket) {
  use_transport_socket_ = false;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(request_header_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  cleanupUpstreamAndDownstream();
  EXPECT_THAT(waitForAccessLog(access_log_name_), ::testing::HasSubstr("-,-,-"));
}

TEST_F(InternalUpstreamIntegrationTest, InternalConnectionBufSizeTestDefault) {
  initialize();
  EXPECT_LOG_CONTAINS("debug", "Internal client connection buffer size 1048576",
                      internalConnectionBufferSizeTest());
}

TEST_F(InternalUpstreamIntegrationTest, InternalConnectionBufSizeTest128KB) {
  buffer_size_specified_ = true;
  buffer_size_ = 128;
  initialize();
  EXPECT_LOG_CONTAINS("debug", "Internal client connection buffer size 131072",
                      internalConnectionBufferSizeTest());
}

TEST_F(InternalUpstreamIntegrationTest, InternalConnectionBufSizeTest8MB) {
  buffer_size_specified_ = true;
  buffer_size_ = 8192;
  initialize();
  EXPECT_LOG_CONTAINS("debug", "Internal client connection buffer size 8388608",
                      internalConnectionBufferSizeTest());
}

TEST_F(InternalUpstreamIntegrationTest, InternalConnectionBufSizeTest1KB) {
  buffer_size_specified_ = true;
  buffer_size_ = 1;
  initialize();
  EXPECT_LOG_CONTAINS("debug", "Internal client connection buffer size 1024",
                      internalConnectionBufferSizeTest());
}

TEST_F(InternalUpstreamIntegrationTest, InternalConnectionBufSizeTest5KB) {
  buffer_size_specified_ = true;
  buffer_size_ = 5;
  initialize();
  EXPECT_LOG_CONTAINS("debug", "Internal client connection buffer size 5120",
                      internalConnectionBufferSizeTest());
}

} // namespace
} // namespace Envoy
