#include <thread>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/reverse_tunnel/v3/reverse_tunnel.pb.h"
#include "envoy/extensions/transport_sockets/internal_upstream/v3/internal_upstream.pb.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/reverse_connection_utility.h"

#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {
namespace {

class ReverseTunnelFilterIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public BaseIntegrationTest {
public:
  ReverseTunnelFilterIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::baseConfig()) {}

  void initialize() override {
    // Add common bootstrap extensions that are used across multiple tests.
    config_helper_.addBootstrapExtension(R"EOF(
name: envoy.bootstrap.reverse_tunnel.upstream_socket_interface
typed_config:
  "@type": type.googleapis.com/envoy.extensions.bootstrap.reverse_tunnel.upstream_socket_interface.v3.UpstreamReverseConnectionSocketInterface
  enable_detailed_stats: true
)EOF");

    config_helper_.addBootstrapExtension(fmt::format(R"EOF(
name: envoy.bootstrap.reverse_tunnel.downstream_socket_interface
typed_config:
  "@type": type.googleapis.com/envoy.extensions.bootstrap.reverse_tunnel.downstream_socket_interface.v3.DownstreamReverseConnectionSocketInterface
  enable_detailed_stats: true
  http_handshake:
    request_path: "{}"
)EOF",
                                                     downstream_handshake_request_path_));

    // Call parent initialize to complete setup.
    BaseIntegrationTest::initialize();
  }

protected:
  void addSetFilterStateFilter(const std::string& node_id = "integration-test-node",
                               const std::string& cluster_id = "integration-test-cluster",
                               const std::string& tenant_id = "integration-test-tenant") {
    std::string on_new_connection = "";
    if (!node_id.empty()) {
      on_new_connection += fmt::format(R"(
  - object_key: node_id
    factory_key: envoy.string
    format_string:
      text_format_source:
        inline_string: "{}")",
                                       node_id);
    }
    if (!cluster_id.empty()) {
      on_new_connection += fmt::format(R"(
  - object_key: cluster_id
    factory_key: envoy.string
    format_string:
      text_format_source:
        inline_string: "{}")",
                                       cluster_id);
    }
    if (!tenant_id.empty()) {
      on_new_connection += fmt::format(R"(
  - object_key: tenant_id
    factory_key: envoy.string
    format_string:
      text_format_source:
        inline_string: "{}")",
                                       tenant_id);
    }

    const std::string set_filter_state = fmt::format(R"EOF(
name: envoy.filters.network.set_filter_state
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.set_filter_state.v3.Config
  on_new_connection:{}
)EOF",
                                                     on_new_connection);

    config_helper_.addConfigModifier(
        [set_filter_state](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          envoy::config::listener::v3::Filter filter;
          TestUtility::loadFromYaml(set_filter_state, filter);
          ASSERT_GT(bootstrap.mutable_static_resources()->listeners_size(), 0);
          auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);

          // Create a filter chain if one doesn't exist, otherwise clear existing filters.
          if (listener->filter_chains_size() == 0) {
            listener->add_filter_chains();
          } else {
            listener->mutable_filter_chains(0)->clear_filters();
          }

          // Add set_filter_state first.
          listener->mutable_filter_chains(0)->add_filters()->Swap(&filter);
        });
  }

  void addReverseTunnelFilter(bool auto_close_connections = false,
                              const std::string& request_path = "/reverse_connections/request",
                              const std::string& request_method = "GET",
                              const std::string& validation_config = "") {
    const std::string filter_config =
        fmt::format(R"EOF(
        name: envoy.filters.network.reverse_tunnel
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.reverse_tunnel.v3.ReverseTunnel
          ping_interval:
            seconds: 300
          auto_close_connections: {}
          request_path: "{}"
          request_method: {}{}
)EOF",
                    auto_close_connections ? "true" : "false", request_path, request_method,
                    validation_config.empty() ? "" : "\n" + validation_config);

    config_helper_.addConfigModifier(
        [filter_config](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          envoy::config::listener::v3::Filter filter;
          TestUtility::loadFromYaml(filter_config, filter);
          ASSERT_GT(bootstrap.mutable_static_resources()->listeners_size(), 0);
          auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);

          // Create a filter chain if one doesn't exist.
          if (listener->filter_chains_size() == 0) {
            listener->add_filter_chains();
          }

          // Add reverse tunnel filter (either as first filter or after existing filters).
          listener->mutable_filter_chains(0)->add_filters()->Swap(&filter);
        });
  }

  std::string createTestPayload(const std::string& node_uuid = "integration-test-node",
                                const std::string& cluster_uuid = "integration-test-cluster",
                                const std::string& tenant_uuid = "integration-test-tenant") {
    UNREFERENCED_PARAMETER(node_uuid);
    UNREFERENCED_PARAMETER(cluster_uuid);
    UNREFERENCED_PARAMETER(tenant_uuid);
    return std::string();
  }

  std::string createHttpRequest(const std::string& method, const std::string& path,
                                const std::string& body = "") {
    std::string request = fmt::format("{} {} HTTP/1.1\r\n", method, path);
    request += "Host: localhost\r\n";
    request += fmt::format("Content-Length: {}\r\n", body.length());
    request += "\r\n";
    request += body;
    return request;
  }

  std::string createHttpRequestWithRtHeaders(const std::string& method, const std::string& path,
                                             const std::string& node, const std::string& cluster,
                                             const std::string& tenant,
                                             const std::string& body = "") {
    std::string request = fmt::format("{} {} HTTP/1.1\r\n", method, path);
    request += "Host: localhost\r\n";
    request += fmt::format("{}: {}\r\n", "x-envoy-reverse-tunnel-node-id", node);
    request += fmt::format("{}: {}\r\n", "x-envoy-reverse-tunnel-cluster-id", cluster);
    request += fmt::format("{}: {}\r\n", "x-envoy-reverse-tunnel-tenant-id", tenant);
    request += fmt::format("Content-Length: {}\r\n", body.length());
    request += "\r\n";
    request += body;
    return request;
  }

  void runEndToEndReverseConnectionHandshakeScenario();

  std::string downstream_handshake_request_path_ =
      std::string(Extensions::Bootstrap::ReverseConnection::ReverseConnectionUtility::
                      DEFAULT_REVERSE_TUNNEL_REQUEST_PATH);
  std::string upstream_request_path_ =
      std::string(Extensions::Bootstrap::ReverseConnection::ReverseConnectionUtility::
                      DEFAULT_REVERSE_TUNNEL_REQUEST_PATH);

  // Set log level to debug for this test class.
  LogLevelSetter log_level_setter_ = LogLevelSetter(spdlog::level::trace);
};

void ReverseTunnelFilterIntegrationTest::runEndToEndReverseConnectionHandshakeScenario() {
  const uint32_t upstream_port = GetParam() == Network::Address::IpVersion::v4 ? 15000 : 15001;
  const std::string loopback_addr =
      GetParam() == Network::Address::IpVersion::v4 ? "127.0.0.1" : "::1";

  config_helper_.addConfigModifier([this, upstream_port, loopback_addr](
                                       envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    bootstrap.mutable_static_resources()->clear_listeners();

    if (!bootstrap.has_admin()) {
      auto* admin = bootstrap.mutable_admin();
      auto* admin_address = admin->mutable_address()->mutable_socket_address();
      admin_address->set_address(loopback_addr);
      admin_address->set_port_value(0);
    }

    auto* upstream_listener = bootstrap.mutable_static_resources()->add_listeners();
    upstream_listener->set_name("upstream_listener");
    upstream_listener->mutable_address()->mutable_socket_address()->set_address(loopback_addr);
    upstream_listener->mutable_address()->mutable_socket_address()->set_port_value(upstream_port);

    auto* upstream_chain = upstream_listener->add_filter_chains();
    auto* rt_filter = upstream_chain->add_filters();
    rt_filter->set_name("envoy.filters.network.reverse_tunnel");

    envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel rt_config;
    rt_config.mutable_ping_interval()->set_seconds(300);
    rt_config.set_auto_close_connections(false);
    rt_config.set_request_path(upstream_request_path_);
    rt_config.set_request_method(envoy::config::core::v3::GET);
    rt_filter->mutable_typed_config()->PackFrom(rt_config);

    auto* rc_listener = bootstrap.mutable_static_resources()->add_listeners();
    rc_listener->set_name("reverse_connection_listener");
    auto* rc_address = rc_listener->mutable_address()->mutable_socket_address();
    rc_address->set_address("rc://e2e-node:e2e-cluster:e2e-tenant@upstream_cluster:1");
    rc_address->set_port_value(0);
    rc_address->set_resolver_name("envoy.resolvers.reverse_connection");

    auto* rc_chain = rc_listener->add_filter_chains();
    auto* echo_filter = rc_chain->add_filters();
    echo_filter->set_name("envoy.filters.network.echo");
    auto* echo_config = echo_filter->mutable_typed_config();
    echo_config->set_type_url("type.googleapis.com/envoy.extensions.filters.network.echo.v3.Echo");

    auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
    cluster->set_name("upstream_cluster");
    cluster->set_type(envoy::config::cluster::v3::Cluster::STATIC);
    cluster->mutable_load_assignment()->set_cluster_name("upstream_cluster");

    auto* locality = cluster->mutable_load_assignment()->add_endpoints();
    auto* lb_endpoint = locality->add_lb_endpoints();
    auto* endpoint = lb_endpoint->mutable_endpoint();
    auto* addr = endpoint->mutable_address()->mutable_socket_address();
    addr->set_address(loopback_addr);
    addr->set_port_value(upstream_port);
  });

  initialize();
  registerTestServerPorts({});

  ENVOY_LOG_MISC(info, "Waiting for reverse connections to be established.");
  timeSystem().advanceTimeWait(std::chrono::milliseconds(1000));

  test_server_->waitForGaugeGe("reverse_tunnel_acceptor.nodes.e2e-node", 1);
  test_server_->waitForGaugeGe("reverse_tunnel_acceptor.clusters.e2e-cluster", 1);

  test_server_->waitForCounterGe("reverse_tunnel.handshake.accepted", 1);

  BufferingStreamDecoderPtr admin_response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/drain_listeners", "", Http::CodecType::HTTP1, GetParam());
  EXPECT_TRUE(admin_response->complete());
  EXPECT_EQ("200", admin_response->headers().getStatusValue());

  test_server_->waitForCounterEq("listener_manager.listener_stopped", 2);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, ReverseTunnelFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ReverseTunnelFilterIntegrationTest, ValidReverseTunnelRequest) {
  // Configure the reverse tunnel filter with default settings.
  addReverseTunnelFilter();
  initialize();

  std::string http_request =
      createHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "integration-test-node",
                                     "integration-test-cluster", "integration-test-tenant");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  if (!tcp_client->write(http_request)) {
    // Server may have already sent the response and closed quickly; still verify response.
    tcp_client->waitForData("HTTP/1.1 200 OK");
    return;
  }

  // Should receive HTTP 200 OK response.
  tcp_client->waitForData("HTTP/1.1 200 OK");

  // Since auto_close_connections: false, we need to close the connection manually.
  tcp_client->close();
}

TEST_P(ReverseTunnelFilterIntegrationTest, InvalidReverseTunnelRequest) {
  // Configure the reverse tunnel filter with default settings.
  addReverseTunnelFilter();
  initialize();

  std::string http_request = createHttpRequest("GET", "/health");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  if (!tcp_client->write(http_request)) {
    // Server may have already sent the response and closed.
    tcp_client->waitForDisconnect();
    return;
  }
  // The request should pass through or be handled by other components; connection may close.
  tcp_client->waitForDisconnect();
}

TEST_P(ReverseTunnelFilterIntegrationTest, PartialRequestHandling) {
  // Configure the reverse tunnel filter with default settings.
  addReverseTunnelFilter();
  initialize();

  std::string http_request = createHttpRequestWithRtHeaders(
      "GET", "/reverse_connections/request", "integration-test-node", "integration-test-cluster",
      "integration-test-tenant", "abcdefghijklmno");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));

  // Send request in chunks but ensure the body only completes on the third chunk.
  // Split the HTTP request into headers and body, then stream body in parts.
  const std::string::size_type hdr_end = http_request.find("\r\n\r\n");
  ASSERT_NE(hdr_end, std::string::npos);
  const std::string headers = http_request.substr(0, hdr_end + 4);
  const std::string body = http_request.substr(hdr_end + 4);
  ASSERT_GT(body.size(), 8u);

  const size_t part = body.size() / 4; // Ensure first 2 parts are not enough to complete.
  const std::string body1 = body.substr(0, part);
  const std::string body2 = body.substr(part, part);
  const std::string body3 = body.substr(2 * part);

  // First write: headers + small part of body.
  if (!tcp_client->write(headers + body1, /*end_stream=*/false)) {
    // Server may have already processed and responded; validate response and exit.
    tcp_client->waitForData("HTTP/1.1 200 OK");
    return;
  }
  // Second write: more body but still not complete. If the server already completed,.
  // the write can fail due to disconnect; treat that as acceptable and verify response.
  if (!tcp_client->write(body2, /*end_stream=*/false)) {
    tcp_client->waitForData("HTTP/1.1 200 OK");
    return;
  }
  // Third write: remaining body to complete the request. Same tolerance as above.
  if (!tcp_client->write(body3, /*end_stream=*/false)) {
    tcp_client->waitForData("HTTP/1.1 200 OK");
    return;
  }

  // Should receive complete HTTP response.
  tcp_client->waitForData("HTTP/1.1 200 OK");
  // Server may keep connection open (auto_close_connections: false). Close client side.
  tcp_client->close();
}

TEST_P(ReverseTunnelFilterIntegrationTest, WrongPathReturns404) {
  // Configure the reverse tunnel filter with default settings.
  addReverseTunnelFilter();
  initialize();

  // Test that requesting a different path than configured returns 404.
  // The default configuration uses "/reverse_connections/request" path.
  std::string http_request =
      createHttpRequestWithRtHeaders("GET", "/custom/reverse", "integration-test-node",
                                     "integration-test-cluster", "integration-test-tenant");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  if (!tcp_client->write(http_request)) {
    // Server may have already sent the response and closed.
    tcp_client->waitForDisconnect();
    return;
  }

  // Should receive 404 Not Found response and connection should close.
  tcp_client->waitForData("HTTP/1.1 404 Not Found");
  tcp_client->waitForDisconnect();
}

TEST_P(ReverseTunnelFilterIntegrationTest, MissingNodeUuidRejection) {
  // Configure the reverse tunnel filter with default settings.
  addReverseTunnelFilter();
  initialize();

  // Missing node UUID header should trigger 400.
  std::string http_request =
      fmt::format("{} {} HTTP/1.1\r\nHost: localhost\r\n"
                  "x-envoy-reverse-tunnel-cluster-id: {}\r\n"
                  "x-envoy-reverse-tunnel-tenant-id: {}\r\nContent-Length: 0\r\n\r\n",
                  "GET", "/reverse_connections/request", "test-cluster", "test-tenant");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  if (!tcp_client->write(http_request)) {
    // Server may have already sent the response and closed.
    tcp_client->waitForData("HTTP/1.1 400 Bad Request");
    return;
  }

  // Should receive HTTP 400 Bad Request response for missing node UUID.
  tcp_client->waitForData("HTTP/1.1 400 Bad Request");
  tcp_client->waitForDisconnect();
}

// Filter accepts when method/path/headers match.
TEST_P(ReverseTunnelFilterIntegrationTest, AcceptsWhenHeadersPresent) {
  addReverseTunnelFilter();
  initialize();

  std::string http_request =
      createHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "integration-test-node",
                                     "integration-test-cluster", "integration-test-tenant");
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write(http_request));
  tcp_client->waitForData("HTTP/1.1 200 OK");
  tcp_client->close();
}

TEST_P(ReverseTunnelFilterIntegrationTest, IgnoresFilterStateValues) {
  addReverseTunnelFilter();
  initialize();

  std::string http_request =
      createHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "integration-test-node",
                                     "integration-test-cluster", "integration-test-tenant");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write(http_request));
  tcp_client->waitForData("HTTP/1.1 200 OK");
  tcp_client->close();
}

// Integration test that verifies basic reverse tunnel handshake.
TEST_P(ReverseTunnelFilterIntegrationTest, BasicReverseTunnelHandshake) {
  // Configure the reverse tunnel filter with default settings.
  addReverseTunnelFilter();
  initialize();

  // Test reverse tunnel handshake and socket reuse functionality.
  std::string http_request = createHttpRequestWithRtHeaders(
      "GET", "/reverse_connections/request", "test-node", "test-cluster", "test-tenant");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write(http_request));

  // Should receive HTTP 200 OK response from the reverse tunnel filter.
  tcp_client->waitForData("HTTP/1.1 200 OK");

  // Verify stats show successful reverse tunnel handshake.
  test_server_->waitForCounterGe("reverse_tunnel.handshake.accepted", 1);

  // Send a second request to test socket caching for different node IDs.
  IntegrationTcpClientPtr tcp_client2 = makeTcpConnection(lookupPort("listener_0"));
  std::string http_request2 = createHttpRequestWithRtHeaders(
      "GET", "/reverse_connections/request", "test-node-2", "test-cluster-2", "test-tenant-2");

  ASSERT_TRUE(tcp_client2->write(http_request2));
  tcp_client2->waitForData("HTTP/1.1 200 OK");

  // Verify additional handshake was processed.
  test_server_->waitForCounterGe("reverse_tunnel.handshake.accepted", 2);

  tcp_client->close();
  tcp_client2->close();
}

// End-to-end reverse connection handshake test where the downstream reverse connection listener
// (rc://) initiates a. connection to upstream listener running the reverse_tunnel filter. The
// downstream. side sends HTTP headers using the same helpers as the upstream expects, and the
// upstream. socket manager updates connection stats. We verify the gauges to confirm handshake
// success. The ping interval is kept at a very high value (5 minutes) to avoid ping timeout on
// accepted reverse connections.
TEST_P(ReverseTunnelFilterIntegrationTest, EndToEndReverseConnectionHandshake) {
  DISABLE_IF_ADMIN_DISABLED; // Test requires admin interface for draining listener.
  runEndToEndReverseConnectionHandshakeScenario();
}

TEST_P(ReverseTunnelFilterIntegrationTest, EndToEndReverseConnectionHandshakeCustomRequestPath) {
  DISABLE_IF_ADMIN_DISABLED;
  downstream_handshake_request_path_ = "/custom/reverse";
  upstream_request_path_ = downstream_handshake_request_path_;
  runEndToEndReverseConnectionHandshakeScenario();
}

// Test validation with static expected values.
TEST_P(ReverseTunnelFilterIntegrationTest, ValidationWithStaticValuesSuccess) {
  const std::string validation_config = R"(
          validation:
            node_id_format: "test-node"
            cluster_id_format: "test-cluster"
            emit_dynamic_metadata: true)";

  addReverseTunnelFilter(false, "/reverse_connections/request", "GET", validation_config);
  initialize();

  std::string http_request = createHttpRequestWithRtHeaders(
      "GET", "/reverse_connections/request", "test-node", "test-cluster", "test-tenant");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write(http_request));
  tcp_client->waitForData("HTTP/1.1 200 OK");
  tcp_client->close();

  test_server_->waitForCounterGe("reverse_tunnel.handshake.accepted", 1);
}

// Test validation with static expected values.
TEST_P(ReverseTunnelFilterIntegrationTest, ValidationWithStaticValuesFailure) {
  const std::string validation_config = R"(
          validation:
            node_id_format: "expected-node"
            cluster_id_format: "expected-cluster")";

  addReverseTunnelFilter(false, "/reverse_connections/request", "GET", validation_config);
  initialize();

  std::string http_request = createHttpRequestWithRtHeaders(
      "GET", "/reverse_connections/request", "wrong-node", "wrong-cluster", "test-tenant");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  (void)tcp_client->write(http_request);
  tcp_client->waitForData("HTTP/1.1 403 Forbidden");
  tcp_client->waitForDisconnect();

  test_server_->waitForCounterGe("reverse_tunnel.handshake.validation_failed", 1);
}

// Test validation with only node_id validation.
TEST_P(ReverseTunnelFilterIntegrationTest, ValidationOnlyNodeId) {
  const std::string validation_config = R"(
          validation:
            node_id_format: "expected-node")";

  addReverseTunnelFilter(false, "/reverse_connections/request", "GET", validation_config);
  initialize();

  // Success: node_id matches, cluster_id ignored.
  std::string http_request_pass = createHttpRequestWithRtHeaders(
      "GET", "/reverse_connections/request", "expected-node", "any-cluster", "test-tenant");

  IntegrationTcpClientPtr tcp_client1 = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client1->write(http_request_pass));
  tcp_client1->waitForData("HTTP/1.1 200 OK");
  tcp_client1->close();

  test_server_->waitForCounterGe("reverse_tunnel.handshake.accepted", 1);

  // Failure: node_id doesn't match.
  std::string http_request_fail = createHttpRequestWithRtHeaders(
      "GET", "/reverse_connections/request", "wrong-node", "any-cluster", "test-tenant");

  IntegrationTcpClientPtr tcp_client2 = makeTcpConnection(lookupPort("listener_0"));
  (void)tcp_client2->write(http_request_fail);
  tcp_client2->waitForData("HTTP/1.1 403 Forbidden");
  tcp_client2->waitForDisconnect();

  test_server_->waitForCounterGe("reverse_tunnel.handshake.validation_failed", 1);
}

// Test validation with only cluster_id validation.
TEST_P(ReverseTunnelFilterIntegrationTest, ValidationOnlyClusterId) {
  const std::string validation_config = R"(
          validation:
            cluster_id_format: "expected-cluster")";

  addReverseTunnelFilter(false, "/reverse_connections/request", "GET", validation_config);
  initialize();

  // Success: cluster_id matches, node_id ignored.
  std::string http_request_pass = createHttpRequestWithRtHeaders(
      "GET", "/reverse_connections/request", "any-node", "expected-cluster", "test-tenant");

  IntegrationTcpClientPtr tcp_client1 = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client1->write(http_request_pass));
  tcp_client1->waitForData("HTTP/1.1 200 OK");
  tcp_client1->close();

  test_server_->waitForCounterGe("reverse_tunnel.handshake.accepted", 1);

  // Failure: cluster_id doesn't match.
  std::string http_request_fail = createHttpRequestWithRtHeaders(
      "GET", "/reverse_connections/request", "any-node", "wrong-cluster", "test-tenant");

  IntegrationTcpClientPtr tcp_client2 = makeTcpConnection(lookupPort("listener_0"));
  (void)tcp_client2->write(http_request_fail);
  tcp_client2->waitForData("HTTP/1.1 403 Forbidden");
  tcp_client2->waitForDisconnect();

  test_server_->waitForCounterGe("reverse_tunnel.handshake.validation_failed", 1);
}

// Test validation with empty format strings. In this case validation is skipped.
TEST_P(ReverseTunnelFilterIntegrationTest, ValidationWithEmptyFormatters) {
  const std::string validation_config = R"(
          validation:
            node_id_format: ""
            cluster_id_format: "")";

  addReverseTunnelFilter(false, "/reverse_connections/request", "GET", validation_config);
  initialize();

  // Should succeed since no validation is configured.
  std::string http_request = createHttpRequestWithRtHeaders(
      "GET", "/reverse_connections/request", "any-node", "any-cluster", "test-tenant");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write(http_request));
  tcp_client->waitForData("HTTP/1.1 200 OK");
  tcp_client->close();

  test_server_->waitForCounterGe("reverse_tunnel.handshake.accepted", 1);
}

// Test validation with dynamic metadata emission.
TEST_P(ReverseTunnelFilterIntegrationTest, ValidationWithDynamicMetadataEmission) {
  const std::string validation_config = R"(
          validation:
            node_id_format: "test-node"
            cluster_id_format: "test-cluster"
            emit_dynamic_metadata: true
            dynamic_metadata_namespace: "envoy.test.reverse_tunnel")";

  addReverseTunnelFilter(false, "/reverse_connections/request", "GET", validation_config);
  initialize();

  std::string http_request = createHttpRequestWithRtHeaders(
      "GET", "/reverse_connections/request", "test-node", "test-cluster", "test-tenant");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write(http_request));
  tcp_client->waitForData("HTTP/1.1 200 OK");
  tcp_client->close();

  test_server_->waitForCounterGe("reverse_tunnel.handshake.accepted", 1);
}

// Test validation with multiple formatters in format string.
TEST_P(ReverseTunnelFilterIntegrationTest, ValidationWithComplexFormatString) {
  const std::string validation_config = R"(
          validation:
            node_id_format: "prefix-%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%-suffix"
            emit_dynamic_metadata: false)";

  addReverseTunnelFilter(false, "/reverse_connections/request", "GET", validation_config);
  initialize();

  // This should fail since node_id won't match the complex format string.
  std::string http_request = createHttpRequestWithRtHeaders(
      "GET", "/reverse_connections/request", "simple-node", "test-cluster", "test-tenant");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  (void)tcp_client->write(http_request);
  tcp_client->waitForData("HTTP/1.1 403 Forbidden");
  tcp_client->waitForDisconnect();

  // Ensure the validation_failed counter is updated.
  test_server_->waitForCounterExists("reverse_tunnel.handshake.validation_failed");
  test_server_->waitForCounterGe("reverse_tunnel.handshake.validation_failed", 1);
}

// Test validation passes when formatter returns empty and actual value is empty.
TEST_P(ReverseTunnelFilterIntegrationTest, ValidationWithBothValuesMatching) {
  const std::string validation_config = R"(
          validation:
            node_id_format: "match-node"
            cluster_id_format: "match-cluster")";

  addReverseTunnelFilter(false, "/reverse_connections/request", "GET", validation_config);
  initialize();

  std::string http_request = createHttpRequestWithRtHeaders(
      "GET", "/reverse_connections/request", "match-node", "match-cluster", "test-tenant");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write(http_request));
  tcp_client->waitForData("HTTP/1.1 200 OK");
  tcp_client->close();

  test_server_->waitForCounterGe("reverse_tunnel.handshake.accepted", 1);
}

// Test validation with FILTER_STATE formatter.
TEST_P(ReverseTunnelFilterIntegrationTest, ValidationWithFilterStateSuccess) {
  // Set up filter state with expected values.
  addSetFilterStateFilter("", "", ""); // Clear defaults.

  // Add filter state for expected values that the validator will check against.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    const std::string set_filter_state = R"EOF(
name: envoy.filters.network.set_filter_state
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.set_filter_state.v3.Config
  on_new_connection:
  - object_key: expected_node_id
    factory_key: envoy.string
    format_string:
      text_format_source:
        inline_string: "validated-node"
  - object_key: expected_cluster_id
    factory_key: envoy.string
    format_string:
      text_format_source:
        inline_string: "validated-cluster"
)EOF";

    envoy::config::listener::v3::Filter filter;
    TestUtility::loadFromYaml(set_filter_state, filter);
    ASSERT_GT(bootstrap.mutable_static_resources()->listeners_size(), 0);
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);

    if (listener->filter_chains_size() == 0) {
      listener->add_filter_chains();
    } else {
      listener->mutable_filter_chains(0)->clear_filters();
    }

    listener->mutable_filter_chains(0)->add_filters()->Swap(&filter);
  });

  // Configure validation to use FILTER_STATE formatters with PLAIN specifier to get raw strings.
  const std::string validation_config = R"(
          validation:
            node_id_format: "%FILTER_STATE(expected_node_id:PLAIN)%"
            cluster_id_format: "%FILTER_STATE(expected_cluster_id:PLAIN)%")";

  addReverseTunnelFilter(false, "/reverse_connections/request", "GET", validation_config);
  initialize();

  // Send request with headers matching filter state values.
  std::string http_request = createHttpRequestWithRtHeaders(
      "GET", "/reverse_connections/request", "validated-node", "validated-cluster", "test-tenant");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write(http_request));
  tcp_client->waitForData("HTTP/1.1 200 OK");
  tcp_client->close();

  test_server_->waitForCounterGe("reverse_tunnel.handshake.accepted", 1);
}

// Test validation with FILTER_STATE formatter.
TEST_P(ReverseTunnelFilterIntegrationTest, ValidationWithFilterStateFailure) {
  // Set up filter state with expected values.
  addSetFilterStateFilter("", "", ""); // Clear defaults.

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    const std::string set_filter_state = R"EOF(
name: envoy.filters.network.set_filter_state
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.set_filter_state.v3.Config
  on_new_connection:
  - object_key: expected_node_id
    factory_key: envoy.string
    format_string:
      text_format_source:
        inline_string: "validated-node"
  - object_key: expected_cluster_id
    factory_key: envoy.string
    format_string:
      text_format_source:
        inline_string: "validated-cluster"
)EOF";

    envoy::config::listener::v3::Filter filter;
    TestUtility::loadFromYaml(set_filter_state, filter);
    ASSERT_GT(bootstrap.mutable_static_resources()->listeners_size(), 0);
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);

    if (listener->filter_chains_size() == 0) {
      listener->add_filter_chains();
    } else {
      listener->mutable_filter_chains(0)->clear_filters();
    }

    listener->mutable_filter_chains(0)->add_filters()->Swap(&filter);
  });

  // Configure validation to use FILTER_STATE formatters with PLAIN specifier to get raw strings.
  const std::string validation_config = R"(
          validation:
            node_id_format: "%FILTER_STATE(expected_node_id:PLAIN)%"
            cluster_id_format: "%FILTER_STATE(expected_cluster_id:PLAIN)%")";

  addReverseTunnelFilter(false, "/reverse_connections/request", "GET", validation_config);
  initialize();

  // Send request with headers NOT matching filter state values.
  std::string http_request = createHttpRequestWithRtHeaders(
      "GET", "/reverse_connections/request", "wrong-node", "wrong-cluster", "test-tenant");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  (void)tcp_client->write(http_request);
  tcp_client->waitForData("HTTP/1.1 403 Forbidden");
  tcp_client->waitForDisconnect();

  test_server_->waitForCounterGe("reverse_tunnel.handshake.validation_failed", 1);
}

// Helper network filter to set dynamic metadata for testing.
class MetadataSetterFilter : public Network::ReadFilter {
public:
  explicit MetadataSetterFilter(const std::string& namespace_key,
                                const std::map<std::string, std::string>& metadata_values)
      : namespace_key_(namespace_key), metadata_values_(metadata_values) {}

  Network::FilterStatus onData(Buffer::Instance&, bool) override {
    return Network::FilterStatus::Continue;
  }

  Network::FilterStatus onNewConnection() override {
    // Set dynamic metadata.
    if (!metadata_values_.empty()) {
      Protobuf::Struct metadata_struct;
      auto& fields = *metadata_struct.mutable_fields();

      for (const auto& [key, value] : metadata_values_) {
        fields[key].set_string_value(value);
      }

      read_callbacks_->connection().streamInfo().setDynamicMetadata(namespace_key_,
                                                                    metadata_struct);
    }

    return Network::FilterStatus::Continue;
  }

  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

private:
  Network::ReadFilterCallbacks* read_callbacks_{};
  const std::string namespace_key_;
  const std::map<std::string, std::string> metadata_values_;
};

// Config factory for MetadataSetterFilter.
class MetadataSetterFilterConfig : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  std::string name() const override { return "envoy.test.metadata_setter"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }

  absl::StatusOr<Network::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& proto,
                               Server::Configuration::FactoryContext&) override {
    const auto& config = dynamic_cast<const Protobuf::Struct&>(proto);

    // Extract namespace and metadata from config.
    std::string namespace_key = "envoy.test.reverse_tunnel";
    std::map<std::string, std::string> metadata_values;

    if (config.fields().contains("namespace")) {
      namespace_key = config.fields().at("namespace").string_value();
    }

    if (config.fields().contains("metadata")) {
      const auto& metadata_struct = config.fields().at("metadata").struct_value();
      for (const auto& [key, value] : metadata_struct.fields()) {
        metadata_values[key] = value.string_value();
      }
    }

    return [namespace_key, metadata_values](Network::FilterManager& filter_manager) {
      filter_manager.addReadFilter(
          std::make_shared<MetadataSetterFilter>(namespace_key, metadata_values));
    };
  }
};

// Register the metadata setter filter factory.
static Registry::RegisterFactory<MetadataSetterFilterConfig,
                                 Server::Configuration::NamedNetworkFilterConfigFactory>
    register_metadata_setter_;

// Test validation with DYNAMIC_METADATA formatter.
TEST_P(ReverseTunnelFilterIntegrationTest, ValidationWithDynamicMetadataSuccess) {
  // Add metadata setter filter to populate dynamic metadata.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Create the Protobuf::Struct config programmatically.
    Protobuf::Struct filter_config;
    (*filter_config.mutable_fields())["namespace"].set_string_value("envoy.test.reverse_tunnel");

    auto* metadata_struct = (*filter_config.mutable_fields())["metadata"].mutable_struct_value();
    (*metadata_struct->mutable_fields())["expected_node_id"].set_string_value(
        "meta-validated-node");
    (*metadata_struct->mutable_fields())["expected_cluster_id"].set_string_value(
        "meta-validated-cluster");

    envoy::config::listener::v3::Filter filter;
    filter.set_name("envoy.test.metadata_setter");
    filter.mutable_typed_config()->PackFrom(filter_config);

    ASSERT_GT(bootstrap.mutable_static_resources()->listeners_size(), 0);
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);

    if (listener->filter_chains_size() == 0) {
      listener->add_filter_chains();
    } else {
      listener->mutable_filter_chains(0)->clear_filters();
    }

    listener->mutable_filter_chains(0)->add_filters()->Swap(&filter);
  });

  // Configure validation to use DYNAMIC_METADATA formatters.
  const std::string validation_config = R"(
          validation:
            node_id_format: "%DYNAMIC_METADATA(envoy.test.reverse_tunnel:expected_node_id)%"
            cluster_id_format: "%DYNAMIC_METADATA(envoy.test.reverse_tunnel:expected_cluster_id)%")";

  addReverseTunnelFilter(false, "/reverse_connections/request", "GET", validation_config);
  initialize();

  // Send request with headers matching dynamic metadata values.
  std::string http_request =
      createHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "meta-validated-node",
                                     "meta-validated-cluster", "test-tenant");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write(http_request));
  tcp_client->waitForData("HTTP/1.1 200 OK");
  tcp_client->close();

  test_server_->waitForCounterGe("reverse_tunnel.handshake.accepted", 1);
}

// Test validation with DYNAMIC_METADATA formatter.
TEST_P(ReverseTunnelFilterIntegrationTest, ValidationWithDynamicMetadataFailure) {
  // Add metadata setter filter to populate dynamic metadata.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Create the Protobuf::Struct config programmatically.
    Protobuf::Struct filter_config;
    (*filter_config.mutable_fields())["namespace"].set_string_value("envoy.test.reverse_tunnel");

    auto* metadata_struct = (*filter_config.mutable_fields())["metadata"].mutable_struct_value();
    (*metadata_struct->mutable_fields())["expected_node_id"].set_string_value(
        "meta-validated-node");
    (*metadata_struct->mutable_fields())["expected_cluster_id"].set_string_value(
        "meta-validated-cluster");

    envoy::config::listener::v3::Filter filter;
    filter.set_name("envoy.test.metadata_setter");
    filter.mutable_typed_config()->PackFrom(filter_config);

    ASSERT_GT(bootstrap.mutable_static_resources()->listeners_size(), 0);
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);

    if (listener->filter_chains_size() == 0) {
      listener->add_filter_chains();
    } else {
      listener->mutable_filter_chains(0)->clear_filters();
    }

    listener->mutable_filter_chains(0)->add_filters()->Swap(&filter);
  });

  // Configure validation to use DYNAMIC_METADATA formatters.
  const std::string validation_config = R"(
          validation:
            node_id_format: "%DYNAMIC_METADATA(envoy.test.reverse_tunnel:expected_node_id)%"
            cluster_id_format: "%DYNAMIC_METADATA(envoy.test.reverse_tunnel:expected_cluster_id)%")";

  addReverseTunnelFilter(false, "/reverse_connections/request", "GET", validation_config);
  initialize();

  // Send request with headers NOT matching dynamic metadata values.
  std::string http_request = createHttpRequestWithRtHeaders(
      "GET", "/reverse_connections/request", "wrong-node", "wrong-cluster", "test-tenant");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  (void)tcp_client->write(http_request);
  tcp_client->waitForData("HTTP/1.1 403 Forbidden");
  tcp_client->waitForDisconnect();

  test_server_->waitForCounterGe("reverse_tunnel.handshake.validation_failed", 1);
}

// Test validation with mixed FILTER_STATE and DYNAMIC_METADATA formatters.
TEST_P(ReverseTunnelFilterIntegrationTest, ValidationWithMixedFormattersSuccess) {
  // Set up filter state for node_id.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    const std::string set_filter_state = R"EOF(
name: envoy.filters.network.set_filter_state
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.set_filter_state.v3.Config
  on_new_connection:
  - object_key: expected_node_id
    factory_key: envoy.string
    format_string:
      text_format_source:
        inline_string: "fs-node"
)EOF";

    envoy::config::listener::v3::Filter filter;
    TestUtility::loadFromYaml(set_filter_state, filter);
    ASSERT_GT(bootstrap.mutable_static_resources()->listeners_size(), 0);
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);

    if (listener->filter_chains_size() == 0) {
      listener->add_filter_chains();
    } else {
      listener->mutable_filter_chains(0)->clear_filters();
    }

    listener->mutable_filter_chains(0)->add_filters()->Swap(&filter);
  });

  // Add metadata setter filter for cluster_id.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Create the Protobuf::Struct config programmatically.
    Protobuf::Struct filter_config;
    (*filter_config.mutable_fields())["namespace"].set_string_value("envoy.test.reverse_tunnel");

    auto* metadata_struct = (*filter_config.mutable_fields())["metadata"].mutable_struct_value();
    (*metadata_struct->mutable_fields())["expected_cluster_id"].set_string_value("dm-cluster");

    envoy::config::listener::v3::Filter filter;
    filter.set_name("envoy.test.metadata_setter");
    filter.mutable_typed_config()->PackFrom(filter_config);

    ASSERT_GT(bootstrap.mutable_static_resources()->listeners_size(), 0);
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    ASSERT_GT(listener->filter_chains_size(), 0);

    listener->mutable_filter_chains(0)->add_filters()->Swap(&filter);
  });

  // Configure validation to use both FILTER_STATE and DYNAMIC_METADATA formatters.
  const std::string validation_config = R"(
          validation:
            node_id_format: "%FILTER_STATE(expected_node_id:PLAIN)%"
            cluster_id_format: "%DYNAMIC_METADATA(envoy.test.reverse_tunnel:expected_cluster_id)%")";

  addReverseTunnelFilter(false, "/reverse_connections/request", "GET", validation_config);
  initialize();

  // Send request with headers matching both filter state and dynamic metadata values.
  std::string http_request = createHttpRequestWithRtHeaders("GET", "/reverse_connections/request",
                                                            "fs-node", "dm-cluster", "test-tenant");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write(http_request));
  tcp_client->waitForData("HTTP/1.1 200 OK");
  tcp_client->close();

  test_server_->waitForCounterGe("reverse_tunnel.handshake.accepted", 1);
}

// Test validation with mixed formatters.
TEST_P(ReverseTunnelFilterIntegrationTest, ValidationWithMixedFormattersNodeFailure) {
  // Set up filter state for node_id.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    const std::string set_filter_state = R"EOF(
name: envoy.filters.network.set_filter_state
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.set_filter_state.v3.Config
  on_new_connection:
  - object_key: expected_node_id
    factory_key: envoy.string
    format_string:
      text_format_source:
        inline_string: "fs-node"
)EOF";

    envoy::config::listener::v3::Filter filter;
    TestUtility::loadFromYaml(set_filter_state, filter);
    ASSERT_GT(bootstrap.mutable_static_resources()->listeners_size(), 0);
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);

    if (listener->filter_chains_size() == 0) {
      listener->add_filter_chains();
    } else {
      listener->mutable_filter_chains(0)->clear_filters();
    }

    listener->mutable_filter_chains(0)->add_filters()->Swap(&filter);
  });

  // Add metadata setter filter for cluster_id.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Create the Protobuf::Struct config programmatically.
    Protobuf::Struct filter_config;
    (*filter_config.mutable_fields())["namespace"].set_string_value("envoy.test.reverse_tunnel");

    auto* metadata_struct = (*filter_config.mutable_fields())["metadata"].mutable_struct_value();
    (*metadata_struct->mutable_fields())["expected_cluster_id"].set_string_value("dm-cluster");

    envoy::config::listener::v3::Filter filter;
    filter.set_name("envoy.test.metadata_setter");
    filter.mutable_typed_config()->PackFrom(filter_config);

    ASSERT_GT(bootstrap.mutable_static_resources()->listeners_size(), 0);
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    ASSERT_GT(listener->filter_chains_size(), 0);

    listener->mutable_filter_chains(0)->add_filters()->Swap(&filter);
  });

  // Configure validation to use both FILTER_STATE and DYNAMIC_METADATA formatters.
  const std::string validation_config = R"(
          validation:
            node_id_format: "%FILTER_STATE(expected_node_id:PLAIN)%"
            cluster_id_format: "%DYNAMIC_METADATA(envoy.test.reverse_tunnel:expected_cluster_id)%")";

  addReverseTunnelFilter(false, "/reverse_connections/request", "GET", validation_config);
  initialize();

  // Send request with wrong node_id but correct cluster_id. It should fail.
  std::string http_request = createHttpRequestWithRtHeaders(
      "GET", "/reverse_connections/request", "wrong-node", "dm-cluster", "test-tenant");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  (void)tcp_client->write(http_request);
  tcp_client->waitForData("HTTP/1.1 403 Forbidden");
  tcp_client->waitForDisconnect();

  test_server_->waitForCounterGe("reverse_tunnel.handshake.validation_failed", 1);
}

// Test validation with mixed formatters.
TEST_P(ReverseTunnelFilterIntegrationTest, ValidationWithMixedFormattersClusterFailure) {
  // Set up filter state for node_id.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    const std::string set_filter_state = R"EOF(
name: envoy.filters.network.set_filter_state
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.set_filter_state.v3.Config
  on_new_connection:
  - object_key: expected_node_id
    factory_key: envoy.string
    format_string:
      text_format_source:
        inline_string: "fs-node"
)EOF";

    envoy::config::listener::v3::Filter filter;
    TestUtility::loadFromYaml(set_filter_state, filter);
    ASSERT_GT(bootstrap.mutable_static_resources()->listeners_size(), 0);
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);

    if (listener->filter_chains_size() == 0) {
      listener->add_filter_chains();
    } else {
      listener->mutable_filter_chains(0)->clear_filters();
    }

    listener->mutable_filter_chains(0)->add_filters()->Swap(&filter);
  });

  // Add metadata setter filter for cluster_id.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Create the Protobuf::Struct config programmatically.
    Protobuf::Struct filter_config;
    (*filter_config.mutable_fields())["namespace"].set_string_value("envoy.test.reverse_tunnel");

    auto* metadata_struct = (*filter_config.mutable_fields())["metadata"].mutable_struct_value();
    (*metadata_struct->mutable_fields())["expected_cluster_id"].set_string_value("dm-cluster");

    envoy::config::listener::v3::Filter filter;
    filter.set_name("envoy.test.metadata_setter");
    filter.mutable_typed_config()->PackFrom(filter_config);

    ASSERT_GT(bootstrap.mutable_static_resources()->listeners_size(), 0);
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    ASSERT_GT(listener->filter_chains_size(), 0);

    listener->mutable_filter_chains(0)->add_filters()->Swap(&filter);
  });

  // Configure validation to use both FILTER_STATE and DYNAMIC_METADATA formatters.
  const std::string validation_config = R"(
          validation:
            node_id_format: "%FILTER_STATE(expected_node_id:PLAIN)%"
            cluster_id_format: "%DYNAMIC_METADATA(envoy.test.reverse_tunnel:expected_cluster_id)%")";

  addReverseTunnelFilter(false, "/reverse_connections/request", "GET", validation_config);
  initialize();

  // Send request with correct node_id but wrong cluster_id. It should fail.
  std::string http_request = createHttpRequestWithRtHeaders(
      "GET", "/reverse_connections/request", "fs-node", "wrong-cluster", "test-tenant");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  (void)tcp_client->write(http_request);
  tcp_client->waitForData("HTTP/1.1 403 Forbidden");
  tcp_client->waitForDisconnect();

  test_server_->waitForCounterGe("reverse_tunnel.handshake.validation_failed", 1);
}

} // namespace
} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
