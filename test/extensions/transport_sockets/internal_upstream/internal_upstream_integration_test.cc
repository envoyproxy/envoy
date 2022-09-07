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
                                       public HttpIntegrationTest {
public:
  HttpsInspectionIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}
  void initialize() override;
  void setupDnsCacheConfig(envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig&
                           dns_cache_config);
  void setupBootstrapExtension();
  void setupTransportSocket(envoy::config::listener::v3::FilterChain& filter_chain);
  void setupTcpProxyFilter(envoy::config::listener::v3::Filter& filter, const std::string& filter_name,
                           const std::string& cluster_name, const std::string& stats_prefix);
  void setupExternalListenerConfig();
  void setupInternalUpstreamConfig();
  void setupInternalListenerCofnig();
  void setupExternalUpstreamConfig();
  void setupTestConfig();
  void sendHttpPostMsg();
  void sendBidirectionalData();
  void startTlsNegotiation();

  // Contexts needed by raw buffer and tls transport sockets.
  std::unique_ptr<Ssl::ContextManager> tls_context_manager_;
  Network::UpstreamTransportSocketFactoryPtr tls_context_;
  Network::UpstreamTransportSocketFactoryPtr cleartext_context_;

  MockWatermarkBuffer* client_write_buffer_{nullptr};
  ConnectionStatusCallbacks connect_callbacks_;

  std::unique_ptr<ClientTestConnection> conn_;
  std::shared_ptr<WaitForPayloadReader> payload_reader_;
  bool clear_http_test_{false};
  bool external_tcp_proxy_filter_{false};
  bool external_hcm_filter_{false};
  bool internal_tcp_proxy_filter_{false};
  bool internal_hcm_filter_{false};
  std::string request_data_{"foo"};
  std::string response_data_{"bar"};
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

// Setup DNS cache config.
void HttpsInspectionIntegrationTest::setupDnsCacheConfig(
    envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig&
    dns_cache_config) {
  dns_cache_config.set_name("dynamic_forward_proxy_cache_config");
  if (GetParam() == Network::Address::IpVersion::v4) {
    dns_cache_config.set_dns_lookup_family(envoy::config::cluster::v3::Cluster::V4_ONLY);
  } else {
    dns_cache_config.set_dns_lookup_family(envoy::config::cluster::v3::Cluster::V6_ONLY);
  }
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
    auto* tls_params = common_tls_context->mutable_tls_params();
    tls_params->set_tls_minimum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_0);
    tls_params->set_tls_maximum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3);
    auto* tls_certificate = common_tls_context->add_tls_certificates();
    tls_certificate->mutable_certificate_chain()->set_filename(TestEnvironment::runfilesPath("test/config/integration/certs/servercert.pem"));
    tls_certificate->mutable_private_key()->set_filename(TestEnvironment::runfilesPath("test/config/integration/certs/serverkey.pem"));
    transport_socket->mutable_typed_config()->PackFrom(tls_context);
  }
}

void HttpsInspectionIntegrationTest::setupTcpProxyFilter(envoy::config::listener::v3::Filter& filter,
     const std::string& filter_name, const std::string& cluster_name, const std::string& stats_prefix) {
  filter.set_name(filter_name);
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
  tcp_proxy.set_cluster(cluster_name);
  tcp_proxy.set_stat_prefix(stats_prefix);
  filter.mutable_typed_config()->PackFrom(tcp_proxy);
}

// Setup the external listener which listens to the client messages.
void HttpsInspectionIntegrationTest::setupExternalListenerConfig() {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    auto* listener = static_resources->mutable_listeners(0);
    // Setup listener filter chain.
    auto* filter_chain = listener->mutable_filter_chains(0);
    // Clear existing http proxy filters.
    filter_chain->clear_filters();
    auto* filter = filter_chain->add_filters();
    if (external_hcm_filter_) {
      envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager hcm;
      hcm.set_stat_prefix("stats_prefix");
      auto * route_config = hcm.mutable_route_config();
      route_config->set_name("local_route");
      auto * virtual_hosts = route_config->add_virtual_hosts();
      virtual_hosts->set_name("local_service");
      virtual_hosts->add_domains("*");
      // 1st route for CONNECT message.
      auto * routes = virtual_hosts->add_routes();
      ConfigHelper::setConnectConfig(hcm, true, false);
      routes->mutable_route()->set_cluster("internal_upstream");
      // 2nd route.
      routes = virtual_hosts->add_routes();
      routes->mutable_match()->set_prefix("/");
      routes->mutable_route()->set_cluster("internal_upstream");

      // Setup router HTTP filter.
      auto* router_filter = hcm.add_http_filters();
      router_filter->set_name("envoy.filters.http.router");
      envoy::extensions::filters::http::router::v3::Router router;
      router_filter->mutable_typed_config()->PackFrom(router);
      filter->set_name("envoy.filters.network.http_connection_manager");
      filter->mutable_typed_config()->PackFrom(hcm);
    } else if (external_tcp_proxy_filter_) {
      setupTcpProxyFilter(*filter, "tcp_proxy", "internal_upstream", "stats_prefix");
    }
  });
}

// Setup the internal upstream cluster which bridge the traffic back to internal listener.
void HttpsInspectionIntegrationTest::setupInternalUpstreamConfig() {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* static_resources = bootstrap.mutable_static_resources();
    auto* cluster = static_resources->mutable_clusters()->Add();
    cluster->set_name("internal_upstream");
    // Insert internal address endpoint.
    cluster->clear_load_assignment();
    auto* load_assignment = cluster->mutable_load_assignment();
    load_assignment->set_cluster_name(cluster->name());
    auto* endpoints = load_assignment->add_endpoints();
    auto* lb_endpoint = endpoints->add_lb_endpoints();
    auto* endpoint = lb_endpoint->mutable_endpoint();
    auto* addr = endpoint->mutable_address()->mutable_envoy_internal_address();
    addr->set_server_listener_name("internal_address");
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
    auto* filter = filter_chain->add_filters();

    if (internal_hcm_filter_) {
      // Set up HCM.
      envoy::extensions::filters::network::http_connection_manager::
          v3::HttpConnectionManager hcm;
      hcm.set_stat_prefix("stats_prefix");
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
      setupDnsCacheConfig(*dns_cache_config);
      // Setup DFP HTTP filter.
      auto* dfp_filter = hcm.add_http_filters();
      dfp_filter->set_name("envoy.filters.http.dynamic_forward_proxy");
      dfp_filter->mutable_typed_config()->PackFrom(filter_config);
      // Setup router HTTP filter.
      auto* router_filter = hcm.add_http_filters();
      router_filter->set_name("envoy.filters.http.router");
      envoy::extensions::filters::http::router::v3::Router router;
      router_filter->mutable_typed_config()->PackFrom(router);
      filter->set_name("envoy.filters.network.http_connection_manager");
      filter->mutable_typed_config()->PackFrom(hcm);
    } else if (internal_tcp_proxy_filter_) {
      setupTcpProxyFilter(*filter, "tcp_proxy", "cluster_0", "stats_prefix");
    }
  });
}

// Setup the external upstream which has the DFP cluster.
void HttpsInspectionIntegrationTest::setupExternalUpstreamConfig() {
  if (internal_hcm_filter_) {
    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* static_resources = bootstrap.mutable_static_resources();
      auto* cluster_0 = static_resources->mutable_clusters(0);
      cluster_0->mutable_cluster_type()->set_name("envoy.clusters.dynamic_forward_proxy");
      cluster_0->set_lb_policy(envoy::config::cluster::v3::Cluster_LbPolicy_CLUSTER_PROVIDED);

      // Setup DFP ClusterConfig.
      envoy::extensions::clusters::dynamic_forward_proxy::v3::ClusterConfig dfp_cluster_config;
    auto *dns_cache_config = dfp_cluster_config.mutable_dns_cache_config();
    setupDnsCacheConfig(*dns_cache_config);
    cluster_0->mutable_cluster_type()->mutable_typed_config()->PackFrom(dfp_cluster_config);
    cluster_0->clear_load_assignment();
    });
  }
}

void HttpsInspectionIntegrationTest::setupTestConfig() {
  setupBootstrapExtension();
  setupExternalListenerConfig();
  setupInternalUpstreamConfig();
  setupInternalListenerCofnig();
  setupExternalUpstreamConfig();
}

void HttpsInspectionIntegrationTest::sendHttpPostMsg() {
  auto port_str = std::to_string(fake_upstreams_[0]->localAddress()->ip()->port());
  std::string get_msg =
    "POST / HTTP/1.1\r\n"
    "Host: localhost:"+ port_str  + "\r\n"
    "Content-Length: " + std::to_string(request_data_.size()) +
    "\r\n\r\n";
  Buffer::OwnedImpl buffer;
  buffer.add(get_msg);
  conn_->write(buffer, false);
  // Wait for them to arrive upstream.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(
      *dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(
      *dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  EXPECT_EQ(upstream_request_->headers().getMethodValue(), "POST");
  EXPECT_EQ(upstream_request_->headers().getHostValue(), "localhost:" + port_str);
}

void HttpsInspectionIntegrationTest::sendBidirectionalData() {
  Buffer::OwnedImpl buffer;
  buffer.add(request_data_);
  conn_->write(buffer, false);
  // Wait for them to arrive upstream.
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, request_data_));
  // Also test upstream to downstream data.
  payload_reader_->set_data_to_wait_for(response_data_, false);
  upstream_request_->encodeData(response_data_, false);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

void HttpsInspectionIntegrationTest::startTlsNegotiation() {
  conn_->setTransportSocket(tls_context_->createTransportSocket(
      std::make_shared<Network::TransportSocketOptionsImpl>(
          absl::string_view(""), std::vector<std::string>(), std::vector<std::string>{"envoyalpn"}),
      nullptr));
  connect_callbacks_.reset();
  while (!connect_callbacks_.connected() && !connect_callbacks_.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

#if 0
// With TLS enabled in internal listerner, Client sends a clear text HTTP CONNECT message to Envoy.
// After confirms recevied 200 from proxy, client then sends a
// TCP data to Envoy, which is then TCP proxied to the upstream.

TEST_P(HttpsInspectionIntegrationTest, TlsViaHttpConnect) {
  clear_http_test_ = false;
  external_hcm_filter_ = true;
  external_tcp_proxy_filter_ = false;
  internal_hcm_filter_ = false;
  internal_tcp_proxy_filter_ = true;

  setupTestConfig();
  skip_tag_extraction_rule_check_ = true;
  initialize();

  // Open clear-text connection.
  conn_->connect();
  // Sends a HTTP CONNECT message.
  Buffer::OwnedImpl buffer;
  std::string connect_msg =
    "CONNECT www.cnn.com:80 HTTP/1.1\r\n"
    "Host: www.cnn.com:80\r\n"
    "\r\n\r\n";
  payload_reader_->set_data_to_wait_for("HTTP/1.1 200 OK\r\n");
  buffer.add(connect_msg);
  conn_->write(buffer, false);
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  // Sends a HTTP POST message.
  sendHttpPostMsg();

  // Sends a response back to client.
  payload_reader_->set_data_to_wait_for("HTTP/1.1 200 OK\r\n", false);
  upstream_request_->encodeHeaders(default_response_headers_, false);
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  // Make sure the bi-directional data can go through.
  sendBidirectionalData();

  conn_->close(Network::ConnectionCloseType::FlushWrite);
}
#endif


// Client sends a clear text HTTP CONNECT message to Envoy.
// After confirms recevied 200 from proxy, client then sends a
// TCP data to Envoy, which is then TCP proxied to the upstream.

TEST_P(HttpsInspectionIntegrationTest, TcpViaHttpConnect) {
  clear_http_test_ = true;
  external_hcm_filter_ = true;
  external_tcp_proxy_filter_ = false;
  internal_hcm_filter_ = false;
  internal_tcp_proxy_filter_ = true;

  setupTestConfig();
  skip_tag_extraction_rule_check_ = true;
  initialize();

  // Open clear-text connection.
  conn_->connect();
  // Sends a HTTP CONNECT message.
  Buffer::OwnedImpl buffer;
  std::string connect_msg =
    "CONNECT www.cnn.com:80 HTTP/1.1\r\n"
    "Host: www.cnn.com:80\r\n"
    "\r\n\r\n";
  payload_reader_->set_data_to_wait_for("HTTP/1.1 200 OK\r\n");
  buffer.add(connect_msg);
  conn_->write(buffer, false);
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  // Sends a HTTP POST message.
  sendHttpPostMsg();

  // Sends a response back to client.
  payload_reader_->set_data_to_wait_for("HTTP/1.1 200 OK\r\n", false);
  upstream_request_->encodeHeaders(default_response_headers_, false);
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  // Make sure the bi-directional data can go through.
  sendBidirectionalData();

  conn_->close(Network::ConnectionCloseType::FlushWrite);
}


// Client sends a clear text HTTP CONNECT message to Envoy.
// After confirms recevied 200 from proxy, client then sends a
// clear text HTTP GET message, which will hits the DFP filter in the
// internal listener and trigger DNS resolving.

TEST_P(HttpsInspectionIntegrationTest, HttpViaHttpConnect) {
  clear_http_test_ = true;
  external_hcm_filter_ = true;
  external_tcp_proxy_filter_ = false;
  internal_hcm_filter_ = true;
  internal_tcp_proxy_filter_ = false;
  setupTestConfig();
  skip_tag_extraction_rule_check_ = true;
  initialize();

  // Open clear-text connection.
  conn_->connect();
  // Sends a HTTP CONNECT message.
  Buffer::OwnedImpl buffer;
  std::string connect_msg =
    "CONNECT www.cnn.com:80 HTTP/1.1\r\n"
    "Host: www.cnn.com:80\r\n"
    "\r\n\r\n";
  payload_reader_->set_data_to_wait_for("HTTP/1.1 200 OK\r\n");
  buffer.add(connect_msg);
  conn_->write(buffer, false);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  // Sends a HTTP POST message.

  /*
  EXPECT_LOG_CONTAINS("",
                      "main thread resolve complete for host ",
                      sendHttpPostMsg());
  */
  sendHttpPostMsg();

  // Sends a response back to client.
  payload_reader_->set_data_to_wait_for("HTTP/1.1 200 OK\r\n", false);
  upstream_request_->encodeHeaders(default_response_headers_, false);
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  // Make sure the bi-directional data can go through.
  sendBidirectionalData();

  conn_->close(Network::ConnectionCloseType::FlushWrite);
}

TEST_P(HttpsInspectionIntegrationTest, HttpsViaTcpProxy) {
  clear_http_test_ = false;
  external_hcm_filter_ = false;
  external_tcp_proxy_filter_ = true;
  internal_hcm_filter_ = true;
  internal_tcp_proxy_filter_ = false;
  setupTestConfig();
  skip_tag_extraction_rule_check_ = true;
  initialize();

  // Open clear-text connection.
  conn_->connect();
  // Without closing the connection, switch to tls.
  startTlsNegotiation();
  // Sends a HTTP POST message.
  sendHttpPostMsg();

  // Sends a response back to client.
  payload_reader_->set_data_to_wait_for("HTTP/1.1 200 OK\r\n", false);
  upstream_request_->encodeHeaders(default_response_headers_, false);
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  // Make sure the bi-directional data can go through the TLS tunnel.
  sendBidirectionalData();

  conn_->close(Network::ConnectionCloseType::FlushWrite);
}

// Client sends a clear text HTTP CONNECT message to Envoy.
// After confirms recevied 200 from proxy, client then start TLS handshake.
// Then Client then sends a HTTP GET message, which will hit DFP filter in the
// internal listener and trigger DNS resolving.
//
// This test is not working yet since the TLS handshake is failing with below error message:
// remote address:envoy://internal_client_address/,TLS error: 268435703:SSL
// routines:OPENSSL_internal:WRONG_VERSION_NUMBER


TEST_P(HttpsInspectionIntegrationTest, HttpsViaHttpConnect) {
  clear_http_test_ = false;
  external_hcm_filter_ = true;
  external_tcp_proxy_filter_ = false;
  internal_hcm_filter_ = true;
  internal_tcp_proxy_filter_ = false;

  setupTestConfig();
  skip_tag_extraction_rule_check_ = true;
  initialize();
  // Open clear-text connection.
  conn_->connect();

  Buffer::OwnedImpl buffer;
  std::string request_msg;
  request_msg =
    "CONNECT www.cnn.com:80 HTTP/1.1\r\n"
    "Host: www.cnn.com:80\r\n"
    "\r\n\r\n";
  payload_reader_->set_data_to_wait_for("HTTP/1.1 200 OK\r\n");
  buffer.add(request_msg);
  conn_->write(buffer, false);
   // Wait for confirmation
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  // Without closing the connection, switch to tls.
  startTlsNegotiation();

  auto port_str = std::to_string(fake_upstreams_[0]->localAddress()->ip()->port());
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
