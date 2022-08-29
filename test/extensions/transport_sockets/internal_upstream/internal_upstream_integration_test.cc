#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/extensions/transport_sockets/internal_upstream/v3/internal_upstream.pb.h"
#include "envoy/extensions/transport_sockets/raw_buffer/v3/raw_buffer.pb.h"
#include "envoy/extensions/clusters/dynamic_forward_proxy/v3/cluster.pb.h"
#include "envoy/extensions/common/dynamic_forward_proxy/v3/dns_cache.pb.h"
#include "envoy/extensions/filters/http/dynamic_forward_proxy/v3/dynamic_forward_proxy.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.h"

#include "envoy/network/connection.h"

#include "source/common/network/connection_impl.h"
#include "source/extensions/transport_sockets/internal_upstream/config.h"
#include "source/extensions/transport_sockets/raw_buffer/config.h"

#include "test/integration/http_integration.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/resources.h"
#include "test/integration/ssl_utility.h"

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
      shared: true
    )EOF");
    HttpIntegrationTest::initialize();
  }

  bool add_metadata_{true};
  bool use_transport_socket_{true};
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

TEST_F(InternalUpstreamIntegrationTest, BasicFlowWithoutTransportSocket) {
  use_transport_socket_ = false;
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
  EXPECT_THAT(waitForAccessLog(access_log_name_), ::testing::HasSubstr("-,-,-"));
}


// ********************************************************************
// Integration tests for HTTP or HTTPs traffic through HTTP CONNECT
// tunnel using internal listener
// ********************************************************************

// ClientTestConnection is used for simulating a client
// which initiates a connection to Envoy in clear-text and then switches to TLS
// without closing the socket.
class ClientTestConnection : public Network::ClientConnectionImpl {
public:
  ClientTestConnection(Event::Dispatcher& dispatcher,
                       const Network::Address::InstanceConstSharedPtr& remote_address,
                       const Network::Address::InstanceConstSharedPtr& source_address,
                       Network::TransportSocketPtr&& transport_socket,
                       const Network::ConnectionSocket::OptionsSharedPtr& options)
      : ClientConnectionImpl(dispatcher, remote_address, source_address,
                             std::move(transport_socket), options, nullptr) {}

  void setTransportSocket(Network::TransportSocketPtr&& transport_socket) {
    transport_socket_ = std::move(transport_socket);
    transport_socket_->setTransportSocketCallbacks(*this);

    // Reset connection's state machine.
    connecting_ = true;

    // Issue event which will trigger TLS handshake.
    ioHandle().activateFileEvents(Event::FileReadyType::Write);
  }
};

// Fixture class for integration tests.
class HttpsInspectionIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                       public BaseIntegrationTest {
public:
  HttpsInspectionIntegrationTest() : BaseIntegrationTest(GetParam()) {}
  void initialize() override;
  void setupDnsCacheConfigTest( envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig&
                                dns_cache_config);
  void setupBootstrapExtension();
  void setupTransportSocket(envoy::config::listener::v3::FilterChain& filter_chain);
  void setupExternalListenerConfig();
  void setupInternalUpstreamConfig();
  void setupInternalListenerCofnig();
  void setupExternalUpstreamConfig();
  void setupTestConfig();

  // Contexts needed by raw buffer and tls transport sockets.
  std::unique_ptr<Ssl::ContextManager> tls_context_manager_;
  Network::UpstreamTransportSocketFactoryPtr tls_context_;
  Network::UpstreamTransportSocketFactoryPtr cleartext_context_;

  MockWatermarkBuffer* client_write_buffer_{nullptr};
  ConnectionStatusCallbacks connect_callbacks_;

  std::unique_ptr<ClientTestConnection> conn_;
  std::shared_ptr<WaitForPayloadReader> payload_reader_;
  bool clear_http_test_{true};
};

void HttpsInspectionIntegrationTest::initialize() {
  EXPECT_CALL(*mock_buffer_factory_, createBuffer_(_, _, _))
      // Connection constructor will first create write buffer.
      // Test tracks how many bytes are sent.
      .WillOnce(Invoke([&](std::function<void()> below_low, std::function<void()> above_high,
                           std::function<void()> above_overflow) -> Buffer::Instance* {
        client_write_buffer_ =
            new NiceMock<MockWatermarkBuffer>(below_low, above_high, above_overflow);
        ON_CALL(*client_write_buffer_, move(_))
            .WillByDefault(Invoke(client_write_buffer_, &MockWatermarkBuffer::baseMove));
        ON_CALL(*client_write_buffer_, drain(_))
            .WillByDefault(Invoke(client_write_buffer_, &MockWatermarkBuffer::trackDrains));
        return client_write_buffer_;
      }))
      // Connection constructor will also create read buffer, but the test does
      // not track received bytes.
      .WillOnce(Invoke([&](std::function<void()> below_low, std::function<void()> above_high,
                           std::function<void()> above_overflow) -> Buffer::Instance* {
        return new Buffer::WatermarkBuffer(below_low, above_high, above_overflow);
      }));
  config_helper_.renameListener("tcp_proxy");

  // Setup factories and contexts for upstream clear-text raw buffer transport socket.
  auto config = std::make_unique<envoy::extensions::transport_sockets::raw_buffer::v3::RawBuffer>();

  auto factory =
      std::make_unique<Extensions::TransportSockets::RawBuffer::UpstreamRawBufferSocketFactory>();
  cleartext_context_ = Network::UpstreamTransportSocketFactoryPtr{
      factory->createTransportSocketFactory(*config, factory_context_)};

  // Setup factories and contexts for tls transport socket.
  tls_context_manager_ =
      std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(timeSystem());
  tls_context_ = Ssl::createClientSslTransportSocketFactory({}, *tls_context_manager_, *api_);
  payload_reader_ = std::make_shared<WaitForPayloadReader>(*dispatcher_);

  BaseIntegrationTest::initialize();

  Network::Address::InstanceConstSharedPtr address =
      Ssl::getSslAddress(version_, lookupPort("tcp_proxy"));
  conn_ = std::make_unique<ClientTestConnection>(
      *dispatcher_, address, Network::Address::InstanceConstSharedPtr(),
      cleartext_context_->createTransportSocket(
          std::make_shared<Network::TransportSocketOptionsImpl>(
              absl::string_view(""), std::vector<std::string>(), std::vector<std::string>()),
          nullptr),
      nullptr);

  conn_->enableHalfClose(true);
  conn_->addConnectionCallbacks(connect_callbacks_);
  conn_->addReadFilter(payload_reader_);
}

//*************************************************************************
//  Test utility functions used by internal listener integration test
//*************************************************************************

// Test utility function to setup DNS cache config.
void HttpsInspectionIntegrationTest::setupDnsCacheConfigTest(
    envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig&
    dns_cache_config) {
  dns_cache_config.set_name("dynamic_forward_proxy_cache_config");
  dns_cache_config.set_dns_lookup_family(envoy::config::cluster::v3::Cluster::V6_ONLY);
}

// Setup the bootstrap extension config to enable the internal listener feature.
void HttpsInspectionIntegrationTest::setupBootstrapExtension() {
  config_helper_.addBootstrapExtension(R"EOF(
    name: envoy.bootstrap.internal_listener
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.bootstrap.internal_listener.v3.InternalListener
    )EOF");
}

void HttpsInspectionIntegrationTest::setupTransportSocket(envoy::config::listener::v3::FilterChain& filter_chain) {
  if (!clear_http_test_) {
    auto* transport_socket =  filter_chain.mutable_transport_socket();
    transport_socket->set_name("envoy.transport_sockets.tls");
    envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
    auto* common_tls_context = tls_context.mutable_common_tls_context();
    /*
    auto* tls_params = common_tls_context->mutable_tls_params();
    tls_params->set_tls_minimum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_0);
    tls_params->set_tls_maximum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3);
    */
    auto* tls_certificate = common_tls_context->add_tls_certificates();
    tls_certificate->mutable_certificate_chain()->set_filename(TestEnvironment::runfilesPath("test/config/integration/certs/servercert.pem"));
    tls_certificate->mutable_private_key()->set_filename(TestEnvironment::runfilesPath("test/config/integration/certs/serverkey.pem"));
    transport_socket->mutable_typed_config()->PackFrom(tls_context);
  }
}

// Setup the external listener which listens to the client messages.
void HttpsInspectionIntegrationTest::setupExternalListenerConfig() {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    auto* listener = static_resources->mutable_listeners(0);
    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager hcm;
    hcm.set_stat_prefix("ingress_http");
    auto * route_config = hcm.mutable_route_config();
    route_config->set_name("local_route");
    auto * virtual_hosts = route_config->add_virtual_hosts();
    virtual_hosts->set_name("local_service");
    virtual_hosts->add_domains("*");
    auto * routes = virtual_hosts->add_routes();
    routes->mutable_match()->set_prefix("/");
    routes->mutable_route()->set_cluster("internal_upstream");

    // Setup router HTTP filter.
    auto* router_filter = hcm.add_http_filters();
    router_filter->set_name("envoy.filters.http.router");
    envoy::extensions::filters::http::router::v3::Router router;
    router_filter->mutable_typed_config()->PackFrom(router);
    ConfigHelper::setConnectConfig(hcm, true, false);
    // Setup listener filter chain.
    auto* filter_chain = listener->mutable_filter_chains(0);
    // Clear existing http proxy filters.
    filter_chain->clear_filters();
    auto* filter = filter_chain->add_filters();
    filter->set_name("envoy.filters.network.http_connection_manager");
    filter->mutable_typed_config()->PackFrom(hcm);
  });
}

// Setup the internal upstream cluster which bridge the traffic back to internal listener.
void HttpsInspectionIntegrationTest::setupInternalUpstreamConfig() {
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
          transport_socket:
            name: envoy.transport_sockets.raw_buffer
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.transport_sockets.raw_buffer.v3.RawBuffer
        )EOF", *cluster->mutable_transport_socket());
    // Insert internal address endpoint.
    cluster->clear_load_assignment();
    auto* load_assignment = cluster->mutable_load_assignment();
    load_assignment->set_cluster_name(cluster->name());
    auto* endpoints = load_assignment->add_endpoints();
    auto* lb_endpoint = endpoints->add_lb_endpoints();
    auto* endpoint = lb_endpoint->mutable_endpoint();
    auto* addr = endpoint->mutable_address()->mutable_envoy_internal_address();
    addr->set_server_listener_name("internal_address");
    auto* metadata = lb_endpoint->mutable_metadata();
    Config::Metadata::mutableMetadataValue(*metadata, "host_metadata", "value")
            .set_string_value("HOST");
    // Insert cluster metadata.
    Config::Metadata::mutableMetadataValue(*(cluster->mutable_metadata()), "cluster_metadata",
                                           "value").set_string_value("CLUSTER");
  });
}

// Setup the internal listener which listens to the looped back messages.
void HttpsInspectionIntegrationTest::setupInternalListenerCofnig() {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    // Insert internal listener.
    auto* listener = static_resources->mutable_listeners()->Add();
    listener->set_name("internal_address");
    listener->mutable_internal_listener();
    listener->clear_address();
    // Setup listener filter chain.
    auto* filter_chain = listener->add_filter_chains();
    setupTransportSocket(*filter_chain);

    // Set up HCM.
    envoy::extensions::filters::network::http_connection_manager::
                    v3::HttpConnectionManager hcm;
    hcm.set_stat_prefix("ingress_http");
    auto * route_config = hcm.mutable_route_config();
    route_config->set_name("local_route");
    auto * virtual_hosts = route_config->add_virtual_hosts();
    virtual_hosts->set_name("local_service");
    virtual_hosts->add_domains("*");
    auto * routes = virtual_hosts->add_routes();
    routes->mutable_match()->set_prefix("/");
    routes->mutable_route()->set_cluster("cluster_0");
    // Setup DFP FilterConfig.
    envoy::extensions::filters::http::dynamic_forward_proxy::
                    v3::FilterConfig filter_config;
    auto *dns_cache_config = filter_config.mutable_dns_cache_config();
    setupDnsCacheConfigTest(*dns_cache_config);
    // Setup DFP HTTP filter.
    auto* dfp_filter = hcm.add_http_filters();
    dfp_filter->set_name("envoy.filters.http.dynamic_forward_proxy");
    dfp_filter->mutable_typed_config()->PackFrom(filter_config);
    // Setup router HTTP filter.
    auto* router_filter = hcm.add_http_filters();
    router_filter->set_name("envoy.filters.http.router");
    envoy::extensions::filters::http::router::v3::Router router;
    router_filter->mutable_typed_config()->PackFrom(router);
    auto* filter = filter_chain->add_filters();
    filter->set_name("envoy.filters.network.http_connection_manager");
    filter->mutable_typed_config()->PackFrom(hcm);
  });
}

// Setup the external upstream which has the DFP cluster.
void HttpsInspectionIntegrationTest::setupExternalUpstreamConfig() {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    auto* cluster_0 = static_resources->mutable_clusters(0);
    cluster_0->mutable_cluster_type()->set_name("envoy.clusters.dynamic_forward_proxy");
    cluster_0->set_lb_policy(envoy::config::cluster::v3::Cluster_LbPolicy_CLUSTER_PROVIDED);

    // Setup DFP ClusterConfig.
    envoy::extensions::clusters::dynamic_forward_proxy::v3::ClusterConfig dfp_cluster_config;
    auto *dns_cache_config = dfp_cluster_config.mutable_dns_cache_config();
    setupDnsCacheConfigTest(*dns_cache_config);
    cluster_0->mutable_cluster_type()->mutable_typed_config()->PackFrom(dfp_cluster_config);
    cluster_0->clear_load_assignment();
  });
}

void HttpsInspectionIntegrationTest::setupTestConfig() {
  setupBootstrapExtension();
  setupExternalListenerConfig();
  setupInternalUpstreamConfig();
  setupInternalListenerCofnig();
  setupExternalUpstreamConfig();
}

// Client sends a clear text HTTP CONNECT message to Envoy.
// After confirms recevied 200 from proxy, client then sends a
// clear text HTTP GET message, which will hits the DFP filter in the
// internal listener and trigger DNS resolving.

TEST_P(HttpsInspectionIntegrationTest, HttpClearInHttpTunnelTest) {
  setupTestConfig();
  skip_tag_extraction_rule_check_ = true;
  initialize();

  // Open clear-text connection.
  conn_->connect();
  Buffer::OwnedImpl buffer;
  std::string request_msg =
    "CONNECT www.cnn.com:80 HTTP/1.1\r\n"
    "Host: www.cnn.com:80\r\n"
    "\r\n\r\n";
  payload_reader_->set_data_to_wait_for("HTTP/1.1 200 OK");
  buffer.add(request_msg);
  conn_->write(buffer, false);
   // Wait for confirmation
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  // Send few messages over encrypted connection.
  auto port_str = std::to_string(fake_upstreams_[0]->localAddress()->ip()->port());
  request_msg.clear();
  request_msg =
    "GET / HTTP/1.1\r\n"
    "Host: localhost:"+ port_str  + "\r\n"
    "\r\n\r\n";
  buffer.add(request_msg);
  conn_->write(buffer, false);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  conn_->close(Network::ConnectionCloseType::FlushWrite);
}

// Client sends a clear text HTTP CONNECT message to Envoy.
// After confirms recevied 200 from proxy, client then start TLS handshake.
// Then Client then sends a HTTP GET message, which will hit DFP filter in the
// internal listener and trigger DNS resolving.
//
// This test is not working yet since the TLS handshake is failing with below error message:
// remote address:envoy://internal_client_address/,TLS error: 268435703:SSL routines:OPENSSL_internal:WRONG_VERSION_NUMBER


TEST_P(HttpsInspectionIntegrationTest, HttpsInHttpTunnelTest) {
  clear_http_test_ = false;
  setupTestConfig();
  skip_tag_extraction_rule_check_ = true;
  initialize();
  // Open clear-text connection.
  conn_->connect();
  Buffer::OwnedImpl buffer;
  std::string request_msg =
    "CONNECT www.cnn.com:80 HTTP/1.1\r\n"
    "Host: www.cnn.com:80\r\n"
    "\r\n\r\n";
  payload_reader_->set_data_to_wait_for("HTTP/1.1 200 OK");
  buffer.add(request_msg);
  conn_->write(buffer, false);
   // Wait for confirmation
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  // Without closing the connection, switch to tls.
  conn_->setTransportSocket(tls_context_->createTransportSocket(
      std::make_shared<Network::TransportSocketOptionsImpl>(
          absl::string_view(""), std::vector<std::string>(), std::vector<std::string>{"envoyalpn"}),
      nullptr));
  connect_callbacks_.reset();
  while (!connect_callbacks_.connected() && !connect_callbacks_.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }


  // Send few messages over encrypted connection.
  auto port_str = std::to_string(fake_upstreams_[0]->localAddress()->ip()->port());
  request_msg.clear();
  request_msg =
    "GET / HTTP/1.1\r\n"
    "Host: localhost:"+ port_str  + "\r\n"
    "\r\n\r\n";
  buffer.add(request_msg);
  conn_->write(buffer, false);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  conn_->close(Network::ConnectionCloseType::FlushWrite);
}

INSTANTIATE_TEST_SUITE_P(HttpsInspectionIntegrationTestSuite,
                         HttpsInspectionIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

} // namespace
} // namespace Envoy
