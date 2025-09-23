#include <thread>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/reverse_tunnel/v3/reverse_tunnel.pb.h"
#include "envoy/extensions/transport_sockets/internal_upstream/v3/internal_upstream.pb.h"

#include "source/common/protobuf/protobuf.h"

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
)EOF");

    config_helper_.addBootstrapExtension(R"EOF(
name: envoy.bootstrap.reverse_tunnel.downstream_socket_interface
typed_config:
  "@type": type.googleapis.com/envoy.extensions.bootstrap.reverse_tunnel.downstream_socket_interface.v3.DownstreamReverseConnectionSocketInterface
)EOF");

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
                              const std::string& request_method = "GET") {
    const std::string filter_config =
        fmt::format(R"EOF(
        name: envoy.filters.network.reverse_tunnel
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.reverse_tunnel.v3.ReverseTunnel
          ping_interval:
            seconds: 300
          auto_close_connections: {}
          request_path: "{}"
          request_method: {}
)EOF",
                    auto_close_connections ? "true" : "false", request_path, request_method);

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

  // Set log level to debug for this test class.
  LogLevelSetter log_level_setter_ = LogLevelSetter(spdlog::level::trace);
};

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

  // Use a deterministic port to avoid timing issues.
  const uint32_t upstream_port = GetParam() == Network::Address::IpVersion::v4 ? 15000 : 15001;
  const std::string loopback_addr =
      GetParam() == Network::Address::IpVersion::v4 ? "127.0.0.1" : "::1";

  // Configure listeners and clusters for the full reverse tunnel flow.
  config_helper_.addConfigModifier([upstream_port, loopback_addr](
                                       envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Clear existing listeners and add our custom setup.
    bootstrap.mutable_static_resources()->clear_listeners();

    // Ensure admin interface is configured.
    if (!bootstrap.has_admin()) {
      auto* admin = bootstrap.mutable_admin();
      auto* admin_address = admin->mutable_address()->mutable_socket_address();
      admin_address->set_address(loopback_addr);
      admin_address->set_port_value(0); // Use ephemeral port
    }

    // Listener 1: Upstream listener with reverse tunnel filter (accepts reverse connections).
    auto* upstream_listener = bootstrap.mutable_static_resources()->add_listeners();
    upstream_listener->set_name("upstream_listener");
    upstream_listener->mutable_address()->mutable_socket_address()->set_address(loopback_addr);
    upstream_listener->mutable_address()->mutable_socket_address()->set_port_value(upstream_port);

    auto* upstream_chain = upstream_listener->add_filter_chains();
    auto* rt_filter = upstream_chain->add_filters();
    rt_filter->set_name("envoy.filters.network.reverse_tunnel");

    envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel rt_config;
    rt_config.mutable_ping_interval()->set_seconds(
        300); // Set the ping interval to the max value to avoid ping timeout.
    rt_config.set_auto_close_connections(false);
    rt_config.set_request_path("/reverse_connections/request");
    rt_config.set_request_method(envoy::config::core::v3::GET);
    rt_filter->mutable_typed_config()->PackFrom(rt_config);

    // Listener 2: Reverse connection listener (initiates reverse connections).
    auto* rc_listener = bootstrap.mutable_static_resources()->add_listeners();
    rc_listener->set_name("reverse_connection_listener");
    auto* rc_address = rc_listener->mutable_address()->mutable_socket_address();
    // Use rc:// scheme to trigger reverse connection initiation.
    rc_address->set_address("rc://e2e-node:e2e-cluster:e2e-tenant@upstream_cluster:1");
    rc_address->set_port_value(0); // Not used for rc:// addresses
    rc_address->set_resolver_name("envoy.resolvers.reverse_connection");

    // Add filter chain with echo filter to process the connection.
    auto* rc_chain = rc_listener->add_filter_chains();
    auto* echo_filter = rc_chain->add_filters();
    echo_filter->set_name("envoy.filters.network.echo");
    auto* echo_config = echo_filter->mutable_typed_config();
    echo_config->set_type_url("type.googleapis.com/envoy.extensions.filters.network.echo.v3.Echo");

    // Cluster that points to our upstream listener using regular TCP.
    auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
    cluster->set_name("upstream_cluster");
    cluster->set_type(envoy::config::cluster::v3::Cluster::STATIC);
    cluster->mutable_load_assignment()->set_cluster_name("upstream_cluster");

    // Point to the upstream listener's port.
    auto* locality = cluster->mutable_load_assignment()->add_endpoints();
    auto* lb_endpoint = locality->add_lb_endpoints();
    auto* endpoint = lb_endpoint->mutable_endpoint();
    auto* addr = endpoint->mutable_address()->mutable_socket_address();
    addr->set_address(loopback_addr);
    addr->set_port_value(upstream_port);
  });

  initialize();

  // Register admin port after initialization since we cleared listeners.
  registerTestServerPorts({});

  ENVOY_LOG_MISC(info, "Waiting for reverse connections to be established.");
  // Wait for reverse connections to be established.
  timeSystem().advanceTimeWait(std::chrono::milliseconds(1000));

  // Test that the full flow works by checking upstream socket interface metrics.
  test_server_->waitForGaugeGe("reverse_connections.nodes.e2e-node", 1);
  test_server_->waitForGaugeGe("reverse_connections.clusters.e2e-cluster", 1);

  // Verify stats show successful reverse tunnel handshake.
  test_server_->waitForCounterGe("reverse_tunnel.handshake.accepted", 1);

  // Drain listeners to trigger proper cleanup of ReverseConnectionIOHandle.
  BufferingStreamDecoderPtr admin_response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/drain_listeners", "", Http::CodecType::HTTP1, GetParam());
  EXPECT_TRUE(admin_response->complete());
  EXPECT_EQ("200", admin_response->headers().getStatusValue());

  // Wait for listeners to be stopped, which triggers ReverseConnectionIOHandle cleanup.
  test_server_->waitForCounterEq("listener_manager.listener_stopped",
                                 2); // 2 listeners in this test
}

} // namespace
} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
