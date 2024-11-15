#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/bootstrap/internal_listener/v3/internal_listener.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/extensions/transport_sockets/internal_upstream/v3/internal_upstream.pb.h"
#include "envoy/network/connection.h"

#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/transport_sockets/internal_upstream/config.h"

#include "test/integration/http_integration.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/resources.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

class TestObject1Factory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return "internal_state"; }
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    return std::make_unique<Router::StringAccessorImpl>(data);
  }
};

class TestObject2Factory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return "internal_state_once"; }
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    return std::make_unique<Router::StringAccessorImpl>(data);
  }
};

REGISTER_FACTORY(TestObject1Factory, StreamInfo::FilterState::ObjectFactory);
REGISTER_FACTORY(TestObject2Factory, StreamInfo::FilterState::ObjectFactory);

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
    config_helper_.prependFilter(R"EOF(
    name: header-to-filter-state
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.set_filter_state.v3.Config
      on_request_headers:
      - object_key: internal_state
        format_string:
          text_format_source:
            inline_string: "%REQ(internal-header)%"
        shared_with_upstream: TRANSITIVE
    )EOF");
    config_helper_.prependFilter(R"EOF(
    name: header-to-filter-state
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.set_filter_state.v3.Config
      on_request_headers:
      - object_key: internal_state_once
        format_string:
          text_format_source:
            inline_string: "%REQ(internal-header-once)%"
        shared_with_upstream: ONCE
    )EOF");
    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      setupBootstrapExtension(bootstrap);
      auto* static_resources = bootstrap.mutable_static_resources();
      makeInternalListenerUpstream("internal_listener",
                                   static_resources->mutable_clusters()->Add());
      makeInternalListenerUpstream("internal_listener2",
                                   static_resources->mutable_clusters()->Add());

      // Emit metadata to access logs.
      access_log_name_ = TestEnvironment::temporaryPath(TestUtility::uniqueFilename("1"));
      access_log2_name_ = TestEnvironment::temporaryPath(TestUtility::uniqueFilename("2"));

      // Insert internal listeners.
      TestUtility::loadFromYaml(
          makeInternalListener("internal_listener", "internal_listener2", access_log_name_),
          *static_resources->mutable_listeners()->Add());
      TestUtility::loadFromYaml(
          makeInternalListener("internal_listener2", "cluster_0", access_log2_name_),
          *static_resources->mutable_listeners()->Add());
    });
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void {
          auto* route_config = hcm.mutable_route_config();
          auto* virtual_host = route_config->mutable_virtual_hosts(0);
          auto* route = virtual_host->mutable_routes(0);
          route->mutable_route()->set_cluster("internal_listener");
        });
    HttpIntegrationTest::initialize();
  }

  void makeInternalListenerUpstream(const std::string& listener_name,
                                    envoy::config::cluster::v3::Cluster* cluster) {
    cluster->set_name(listener_name);
    cluster->clear_load_assignment();

    if (upstream_bind_config_) {
      // Set upstream binding config in the internal_upstream cluster.
      auto* source_address = cluster->mutable_upstream_bind_config()->mutable_source_address();
      source_address->set_address("1.2.3.4");
      source_address->set_port_value(0);
    }

    auto* load_assignment = cluster->mutable_load_assignment();
    load_assignment->set_cluster_name(cluster->name());
    auto* endpoints = load_assignment->add_endpoints();
    auto* lb_endpoint = endpoints->add_lb_endpoints();
    auto* endpoint = lb_endpoint->mutable_endpoint();
    auto* addr = endpoint->mutable_address()->mutable_envoy_internal_address();
    addr->set_server_listener_name(listener_name);
    addr->set_endpoint_id("lorem_ipsum");
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
    if (add_metadata_) {
      auto* metadata = lb_endpoint->mutable_metadata();
      Config::Metadata::mutableMetadataValue(*metadata, "host_metadata", "value")
          .set_string_value(listener_name);
      // Insert cluster metadata.
      Config::Metadata::mutableMetadataValue(*(cluster->mutable_metadata()), "cluster_metadata",
                                             "value")
          .set_string_value(listener_name);
    }
  }

  std::string makeInternalListener(const std::string& name, const std::string& cluster,
                                   const std::string& access_log_name) {
    return fmt::format(R"EOF(
      name: {}
      internal_listener: {{}}
      filter_chains:
      - filters:
        - name: tcp_proxy
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
            cluster: {}
            stat_prefix: internal_address
            access_log:
            - name: envoy.file_access_log
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
                path: {}
                log_format:
                  text_format_source:
                    inline_string: "%DYNAMIC_METADATA(host_metadata:value)%,%DYNAMIC_METADATA(cluster_metadata:value)%,%FILTER_STATE(internal_state:PLAIN)%,%FILTER_STATE(internal_state_once:PLAIN)%\n"
      )EOF",
                       name, cluster, access_log_name);
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
      {"internal-header-once", "BAZ"},
  }};
  bool add_metadata_{true};
  bool use_transport_socket_{true};
  bool buffer_size_specified_{false};
  uint32_t buffer_size_{0};
  std::string access_log2_name_;
  bool upstream_bind_config_{false};
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
  EXPECT_THAT(waitForAccessLog(access_log_name_),
              ::testing::HasSubstr("internal_listener,internal_listener,FOO,BAZ"));
  EXPECT_THAT(waitForAccessLog(access_log2_name_),
              ::testing::HasSubstr("internal_listener2,internal_listener2,FOO,-"));
}

// To verify that with the upstream binding configured in the internal_upstream cluster,
// the socket address type of internal client connection is not overridden to be
// Type::Ip. It stays as Type::EnvoyInternal, and with that traffic goes through.
TEST_F(InternalUpstreamIntegrationTest, BasicFlowLookbackClusterHasUpstreamBindConfig) {
  upstream_bind_config_ = true;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(request_header_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  cleanupUpstreamAndDownstream();
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
