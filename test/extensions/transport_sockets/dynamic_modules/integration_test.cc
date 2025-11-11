#include "envoy/extensions/transport_sockets/dynamic_modules/v3/dynamic_modules.pb.h"

#include "source/common/protobuf/protobuf.h"

#include "test/integration/http_integration.h"
#include "test/integration/ssl_utility.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace TransportSockets {

class DynamicModuleTransportSocketIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  DynamicModuleTransportSocketIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {};

  void initializeWithDynamicModuleTransportSocket(bool upstream = true);
  void initializeWithRustlsTlsTransportSocket(bool upstream_side = true);
};

void DynamicModuleTransportSocketIntegrationTest::initializeWithDynamicModuleTransportSocket(
    bool upstream) {
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute(
          "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
      1);

  if (upstream) {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      envoy::extensions::transport_sockets::dynamic_modules::v3::
          DynamicModuleUpstreamTransportSocket upstream_config;
      upstream_config.mutable_dynamic_module_config()->set_name(
          "transport_socket_integration_test");
      upstream_config.set_socket_name("passthrough");
      cluster->mutable_transport_socket()->set_name("envoy.transport_sockets.dynamic_modules");
      cluster->mutable_transport_socket()->mutable_typed_config()->PackFrom(upstream_config);
    });
  } else {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* filter_chain = listener->mutable_filter_chains(0);
      envoy::extensions::transport_sockets::dynamic_modules::v3::
          DynamicModuleDownstreamTransportSocket downstream_config;
      downstream_config.mutable_dynamic_module_config()->set_name(
          "transport_socket_integration_test");
      downstream_config.set_socket_name("passthrough");
      filter_chain->mutable_transport_socket()->set_name("envoy.transport_sockets.dynamic_modules");
      filter_chain->mutable_transport_socket()->mutable_typed_config()->PackFrom(downstream_config);
    });
  }

  initialize();
}

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModuleTransportSocketIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DynamicModuleTransportSocketIntegrationTest, UpstreamPassthrough) {
  initializeWithDynamicModuleTransportSocket(true);

  // Create a client aimed at Envoy's default HTTP port.
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  // Create some request headers.
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};

  // Send the request headers from the client, wait until they are received upstream. When they
  // are received, send the default response headers from upstream and wait until they are
  // received at by client.
  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  // Verify the proxied request was received upstream, as expected.
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  // Verify the proxied response was received downstream, as expected.
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ(0U, response->body().size());
}

TEST_P(DynamicModuleTransportSocketIntegrationTest, UpstreamPassthroughWithBody) {
  initializeWithDynamicModuleTransportSocket(true);

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 10, default_response_headers_, 10);

  // Verify the proxied request was received upstream, as expected.
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(10U, upstream_request_->bodyLength());
  // Verify the proxied response was received downstream, as expected.
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ(10U, response->body().size());
}

TEST_P(DynamicModuleTransportSocketIntegrationTest, DownstreamPassthrough) {
  initializeWithDynamicModuleTransportSocket(false);

  // Create a client aimed at Envoy's default HTTP port.
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  // Create some request headers.
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};

  // Send the request headers from the client, wait until they are received upstream. When they
  // are received, send the default response headers from upstream and wait until they are
  // received at by client.
  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  // Verify the proxied request was received upstream, as expected.
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  // Verify the proxied response was received downstream, as expected.
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ(0U, response->body().size());
}

void DynamicModuleTransportSocketIntegrationTest::initializeWithRustlsTlsTransportSocket(
    bool upstream_side) {
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute(
          "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
      1);

  const std::string rundir = TestEnvironment::runfilesDirectory();

  if (upstream_side) {
    // Enable TLS on the fake upstream using BoringSSL.
    upstream_tls_ = true;

    // Configure Envoy to use rustls dynamic module for upstream connections.
    config_helper_.addConfigModifier([rundir](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      envoy::extensions::transport_sockets::dynamic_modules::v3::
          DynamicModuleUpstreamTransportSocket upstream_config;
      upstream_config.mutable_dynamic_module_config()->set_name("rustls_tls_transport_socket");
      upstream_config.set_socket_name("rustls_client");
      upstream_config.set_sni("foo.lyft.com");
      upstream_config.add_alpn_protocols("http/1.1");
      // Pass CA cert path through socket_config as simple JSON.
      Protobuf::Any socket_config;
      envoy::config::core::v3::DataSource config_datasource;
      config_datasource.set_inline_string(
          R"({"ca_path":")" + rundir +
          R"(/test/config/integration/certs/upstreamcacert.pem","sni":"foo.lyft.com"})");
      socket_config.PackFrom(config_datasource);
      *upstream_config.mutable_socket_config() = socket_config;
      cluster->mutable_transport_socket()->set_name("envoy.transport_sockets.dynamic_modules");
      cluster->mutable_transport_socket()->mutable_typed_config()->PackFrom(upstream_config);
    });
  } else {
    // Configure Envoy downstream to use rustls dynamic module.
    config_helper_.addConfigModifier([rundir](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* filter_chain = listener->mutable_filter_chains(0);
      envoy::extensions::transport_sockets::dynamic_modules::v3::
          DynamicModuleDownstreamTransportSocket downstream_config;
      downstream_config.mutable_dynamic_module_config()->set_name("rustls_tls_transport_socket");
      downstream_config.set_socket_name("rustls_server");
      downstream_config.add_alpn_protocols("http/1.1");
      // Configure TLS certificates using DataSource for consistent JSON handling.
      Protobuf::Any socket_config;
      envoy::config::core::v3::DataSource config_datasource;
      config_datasource.set_inline_string(
          R"({"cert_path":")" + rundir +
          R"(/test/config/integration/certs/servercert.pem","key_path":")" + rundir +
          R"(/test/config/integration/certs/serverkey.pem","ca_path":")" + rundir +
          R"(/test/config/integration/certs/cacert.pem"})");
      socket_config.PackFrom(config_datasource);
      *downstream_config.mutable_socket_config() = socket_config;
      filter_chain->mutable_transport_socket()->set_name("envoy.transport_sockets.dynamic_modules");
      filter_chain->mutable_transport_socket()->mutable_typed_config()->PackFrom(downstream_config);
    });
  }

  initialize();
}

TEST_P(DynamicModuleTransportSocketIntegrationTest, UpstreamRustlsTls) {
  initializeWithRustlsTlsTransportSocket(true);

  // Create a client aimed at Envoy's default HTTP port (non-TLS on downstream side).
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  // Create some request headers.
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};

  // Send the request, which will flow through Envoy's rustls client to the TLS upstream.
  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  // Verify the proxied request was received upstream, as expected.
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  // Verify the proxied response was received downstream, as expected.
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ(0U, response->body().size());
}

TEST_P(DynamicModuleTransportSocketIntegrationTest, UpstreamRustlsTlsWithBody) {
  initializeWithRustlsTlsTransportSocket(true);

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  // Send request with a body to test write buffering and data transfer.
  Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"}};

  auto response =
      sendRequestAndWaitForResponse(request_headers, 1024, default_response_headers_, 512);

  // Verify request with body was received.
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024U, upstream_request_->bodyLength());

  // Verify response with body was received.
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ(512U, response->body().size());
}

} // namespace TransportSockets
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
