#include <thread>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/reverse_tunnel/v3/reverse_tunnel.pb.h"
#include "envoy/extensions/transport_sockets/internal_upstream/v3/internal_upstream.pb.h"

#include "source/common/protobuf/protobuf.h"

#include "test/integration/integration.h"
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
  ReverseTunnelFilterIntegrationTest() : BaseIntegrationTest(GetParam()) {}

  // Do not call initialize() here. Tests will configure filters then call initialize().
  void initializeFilter() {
    // Remove default network filters (e.g., HTTP Connection Manager) to avoid conflicts.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      if (bootstrap.static_resources().listeners_size() > 0) {
        auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
        if (listener->filter_chains_size() > 0) {
          auto* chain = listener->mutable_filter_chains(0);
          chain->clear_filters();
        }
      }
    });
    const std::string filter_config = R"EOF(
name: envoy.filters.network.reverse_tunnel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.reverse_tunnel.v3.ReverseTunnel
  ping_interval:
    seconds: 2
  auto_close_connections: false
  request_path: "/reverse_connections/request"
  request_method: "GET"
)EOF";

    config_helper_.addNetworkFilter(filter_config);
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
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ReverseTunnelFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ReverseTunnelFilterIntegrationTest, ValidReverseTunnelRequest) {
  initializeFilter();
  BaseIntegrationTest::initialize();

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
  tcp_client->waitForDisconnect();
}

TEST_P(ReverseTunnelFilterIntegrationTest, InvalidHttpRequest) {
  initializeFilter();
  BaseIntegrationTest::initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  if (!tcp_client->write("INVALID REQUEST\r\n\r\n")) {
    // Server may have already closed the connection due to codec error.
    tcp_client->waitForDisconnect();
    return;
  }
  // Codec error path does not produce a response; server may close the connection.
  tcp_client->waitForDisconnect();
}

TEST_P(ReverseTunnelFilterIntegrationTest, NonReverseTunnelRequest) {
  initializeFilter();
  BaseIntegrationTest::initialize();

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

TEST_P(ReverseTunnelFilterIntegrationTest, MissingHeadersBadRequest) {
  initializeFilter();
  BaseIntegrationTest::initialize();

  // Missing required headers should produce HTTP 400.
  std::string http_request = createHttpRequest("GET", "/reverse_connections/request", "");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  if (!tcp_client->write(http_request)) {
    // Server may have already sent the response and closed.
    tcp_client->waitForData("HTTP/1.1 400 Bad Request");
    return;
  }

  // Should receive HTTP 400 Bad Request response.
  tcp_client->waitForData("HTTP/1.1 400 Bad Request");
  tcp_client->waitForDisconnect();
}

TEST_P(ReverseTunnelFilterIntegrationTest, PartialRequestHandling) {
  initializeFilter();
  BaseIntegrationTest::initialize();

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
  // Second write: more body but still not complete. If the server already completed,
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

TEST_P(ReverseTunnelFilterIntegrationTest, CustomConfigurationTest) {
  const std::string custom_filter_config = R"EOF(
name: envoy.filters.network.reverse_tunnel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.reverse_tunnel.v3.ReverseTunnel
  ping_interval:
    seconds: 5
  auto_close_connections: false
  request_path: "/custom/reverse"
  request_method: "GET"
)EOF";

  // Remove default network filters (e.g., HTTP Connection Manager) to avoid pulling in HCM.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Register default socket interface for internal addresses and set it as default.
    {
      auto* ext = bootstrap.add_bootstrap_extensions();
      ext->set_name("envoy.extensions.network.socket_interface.default_socket_interface");
      auto* any = ext->mutable_typed_config();
      any->set_type_url("type.googleapis.com/"
                        "envoy.extensions.network.socket_interface.v3.DefaultSocketInterface");
    }
    bootstrap.set_default_socket_interface(
        "envoy.extensions.network.socket_interface.default_socket_interface");
    if (bootstrap.static_resources().listeners_size() > 0) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      if (listener->filter_chains_size() > 0) {
        auto* chain = listener->mutable_filter_chains(0);
        chain->clear_filters();
      }
    }
  });
  config_helper_.addNetworkFilter(custom_filter_config);
  BaseIntegrationTest::initialize();

  std::string http_request =
      createHttpRequestWithRtHeaders("GET", "/custom/reverse", "integration-test-node",
                                     "integration-test-cluster", "integration-test-tenant");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  if (!tcp_client->write(http_request)) {
    // Server may have already sent the response and closed.
    tcp_client->waitForData("HTTP/1.1 200 OK");
    return;
  }

  // Should receive HTTP 200 OK response.
  tcp_client->waitForData("HTTP/1.1 200 OK");

  // With auto_close_connections: false, connection should stay open.
  // Advance simulated time slightly to allow any deferred callbacks to run.
  timeSystem().advanceTimeWait(std::chrono::milliseconds(100));
  tcp_client->close();
}

TEST_P(ReverseTunnelFilterIntegrationTest, MissingNodeUuidRejection) {
  initializeFilter();
  BaseIntegrationTest::initialize();

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

TEST_P(ReverseTunnelFilterIntegrationTest, ValidationSucceedsWithFilterState) {
  // Add a filter to set filter state values, followed by reverse_tunnel with validation.
  const std::string set_filter_state = R"EOF(
name: envoy.filters.network.set_filter_state
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.set_filter_state.v3.Config
  on_new_connection:
  - object_key: node_id
    factory_key: envoy.string
    format_string:
      text_format_source:
        inline_string: "integration-test-node"
  - object_key: cluster_id
    factory_key: envoy.string
    format_string:
      text_format_source:
        inline_string: "integration-test-cluster"
  - object_key: tenant_id
    factory_key: envoy.string
    format_string:
      text_format_source:
        inline_string: "integration-test-tenant"
)EOF";

  const std::string rt_filter = R"EOF(
name: envoy.filters.network.reverse_tunnel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.reverse_tunnel.v3.ReverseTunnel
  ping_interval:
    seconds: 2
  auto_close_connections: false
  request_path: "/reverse_connections/request"
  request_method: "GET"
  validation_config:
    node_id_filter_state_key: "node_id"
    cluster_id_filter_state_key: "cluster_id"
    tenant_id_filter_state_key: "tenant_id"
)EOF";

  // Clear default filters and add in order: set_filter_state then reverse_tunnel.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    if (bootstrap.static_resources().listeners_size() > 0) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      if (listener->filter_chains_size() > 0) {
        auto* chain = listener->mutable_filter_chains(0);
        chain->clear_filters();
      }
    }
  });
  config_helper_.addNetworkFilter(set_filter_state);
  config_helper_.addNetworkFilter(rt_filter);
  BaseIntegrationTest::initialize();

  std::string http_request =
      createHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "integration-test-node",
                                     "integration-test-cluster", "integration-test-tenant");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  if (!tcp_client->write(http_request)) {
    // Server may have already sent the response and closed.
    tcp_client->waitForData("HTTP/1.1 403 Forbidden");
    return;
  }

  tcp_client->waitForData("HTTP/1.1 200 OK");
  tcp_client->close();
}

TEST_P(ReverseTunnelFilterIntegrationTest, ValidationFailsWhenKeyMissing) {
  // Only set cluster/tenant; configure reverse_tunnel to require node_id, causing 403.
  const std::string set_filter_state = R"EOF(
name: envoy.filters.network.set_filter_state
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.set_filter_state.v3.Config
  on_new_connection:
  - object_key: cluster_id
    factory_key: envoy.string
    format_string:
      text_format_source:
        inline_string: "integration-test-cluster"
  - object_key: tenant_id
    factory_key: envoy.string
    format_string:
      text_format_source:
        inline_string: "integration-test-tenant"
)EOF";

  const std::string rt_filter = R"EOF(
name: envoy.filters.network.reverse_tunnel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.reverse_tunnel.v3.ReverseTunnel
  request_path: "/reverse_connections/request"
  request_method: "GET"
  validation_config:
    node_id_filter_state_key: "node_id"
    cluster_id_filter_state_key: "cluster_id"
    tenant_id_filter_state_key: "tenant_id"
)EOF";

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    if (bootstrap.static_resources().listeners_size() > 0) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      if (listener->filter_chains_size() > 0) {
        auto* chain = listener->mutable_filter_chains(0);
        chain->clear_filters();
      }
    }
  });
  config_helper_.addNetworkFilter(set_filter_state);
  config_helper_.addNetworkFilter(rt_filter);
  BaseIntegrationTest::initialize();

  std::string http_request =
      createHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "integration-test-node",
                                     "integration-test-cluster", "integration-test-tenant");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  if (!tcp_client->write(http_request)) {
    // Server may have already sent the response and closed.
    tcp_client->waitForData("HTTP/1.1 403 Forbidden");
    return;
  }

  // Should receive HTTP 403 Forbidden response due to missing node_id in filter state.
  tcp_client->waitForData("HTTP/1.1 403 Forbidden");

  // Advance simulated time slightly to allow internal callbacks to drain.
  timeSystem().advanceTimeWait(std::chrono::milliseconds(50));
  tcp_client->waitForDisconnect();
}

TEST_P(ReverseTunnelFilterIntegrationTest, ValidationFailsOnValueMismatch) {
  // Set keys but with different values than in the handshake request, expect 403.
  const std::string set_filter_state = R"EOF(
name: envoy.filters.network.set_filter_state
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.set_filter_state.v3.Config
  on_new_connection:
  - object_key: node_id
    factory_key: envoy.string
    format_string:
      text_format_source:
        inline_string: "wrong-node"
  - object_key: cluster_id
    factory_key: envoy.string
    format_string:
      text_format_source:
        inline_string: "wrong-cluster"
  - object_key: tenant_id
    factory_key: envoy.string
    format_string:
      text_format_source:
        inline_string: "wrong-tenant"
)EOF";

  const std::string rt_filter = R"EOF(
name: envoy.filters.network.reverse_tunnel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.reverse_tunnel.v3.ReverseTunnel
  request_path: "/reverse_connections/request"
  request_method: "GET"
  validation_config:
    node_id_filter_state_key: "node_id"
    cluster_id_filter_state_key: "cluster_id"
    tenant_id_filter_state_key: "tenant_id"
)EOF";

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    if (bootstrap.static_resources().listeners_size() > 0) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      if (listener->filter_chains_size() > 0) {
        auto* chain = listener->mutable_filter_chains(0);
        chain->clear_filters();
      }
    }
  });
  config_helper_.addNetworkFilter(set_filter_state);
  config_helper_.addNetworkFilter(rt_filter);
  BaseIntegrationTest::initialize();

  std::string http_request =
      createHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "integration-test-node",
                                     "integration-test-cluster", "integration-test-tenant");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write(http_request));
  tcp_client->waitForData("HTTP/1.1 403 Forbidden");

  // Advance simulated time slightly to allow internal callbacks to drain.
  timeSystem().advanceTimeWait(std::chrono::milliseconds(50));
  tcp_client->waitForDisconnect();
}

TEST_P(ReverseTunnelFilterIntegrationTest, AutoCloseConnectionsEnabled) {
  const std::string filter_config = R"EOF(
name: envoy.filters.network.reverse_tunnel
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.reverse_tunnel.v3.ReverseTunnel
  auto_close_connections: true
  request_path: "/reverse_connections/request"
  request_method: "GET"
)EOF";

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    if (bootstrap.static_resources().listeners_size() > 0) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      if (listener->filter_chains_size() > 0) {
        auto* chain = listener->mutable_filter_chains(0);
        chain->clear_filters();
      }
    }
  });
  config_helper_.addNetworkFilter(filter_config);
  BaseIntegrationTest::initialize();

  std::string http_request =
      createHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "integration-test-node",
                                     "integration-test-cluster", "integration-test-tenant");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  if (!tcp_client->write(http_request)) {
    // Server may have already responded and closed.
    tcp_client->waitForData("HTTP/1.1 200 OK");
    return;
  }
  tcp_client->waitForData("HTTP/1.1 200 OK");

  // Advance simulated time slightly to allow internal callbacks to drain.
  timeSystem().advanceTimeWait(std::chrono::milliseconds(50));

  // Server should close the connection automatically.
  tcp_client->waitForDisconnect();
}

// End-to-end test where the downstream reverse connection listener (rc://) initiates a
// connection to an upstream listener running the reverse_tunnel filter. The downstream
// side sends HTTP headers using the same helpers as the upstream expects, and the upstream
// socket manager updates connection stats. We verify the gauges to confirm full flow.
TEST_P(ReverseTunnelFilterIntegrationTest, FullFlowWithDownstreamSocketInterface) {
  // Configure two bootstrap extensions (downstream and upstream socket interfaces),
  // two listeners (upstream reverse_tunnel listener and a reverse connection listener),
  // and a cluster that targets the upstream listener via an internal address.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Add upstream socket interface bootstrap extension.
    {
      auto* ext = bootstrap.add_bootstrap_extensions();
      ext->set_name("envoy.bootstrap.reverse_tunnel.upstream_socket_interface");
      auto* any = ext->mutable_typed_config();
      any->set_type_url("type.googleapis.com/"
                        "envoy.extensions.bootstrap.reverse_tunnel.upstream_socket_interface.v3."
                        "UpstreamReverseConnectionSocketInterface");
    }

    // Add downstream socket interface bootstrap extension.
    {
      auto* ext = bootstrap.add_bootstrap_extensions();
      ext->set_name("envoy.bootstrap.reverse_tunnel.downstream_socket_interface");
      auto* any = ext->mutable_typed_config();
      any->set_type_url("type.googleapis.com/"
                        "envoy.extensions.bootstrap.reverse_tunnel.downstream_socket_interface.v3."
                        "DownstreamReverseConnectionSocketInterface");
    }

    // Ensure we have at least one listener. We will use the first as the upstream listener
    // and clear its filters, then add the reverse_tunnel network filter.
    if (bootstrap.static_resources().listeners_size() == 0) {
      auto* listener = bootstrap.mutable_static_resources()->add_listeners();
      listener->set_name("upstream_listener");
      auto* sock = listener->mutable_address()->mutable_socket_address();
      sock->set_address("0.0.0.0");
      sock->set_port_value(0);
    }

    auto* upstream_listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    upstream_listener->set_name("upstream_listener");
    if (upstream_listener->filter_chains_size() > 0) {
      upstream_listener->mutable_filter_chains(0)->clear_filters();
    } else {
      upstream_listener->add_filter_chains();
    }
    {
      auto* filter = upstream_listener->mutable_filter_chains(0)->add_filters();
      filter->set_name("envoy.filters.network.reverse_tunnel");
      envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel rt_cfg;
      rt_cfg.mutable_ping_interval()->set_seconds(2);
      rt_cfg.set_auto_close_connections(false);
      rt_cfg.set_request_path("/reverse_connections/request");
      rt_cfg.set_request_method("GET");
      Protobuf::Any* typed_config = filter->mutable_typed_config();
      typed_config->PackFrom(rt_cfg);
    }

    // Add an additional listener that uses the rc:// resolver to initiate reverse connections.
    auto* rc_listener = bootstrap.mutable_static_resources()->add_listeners();
    rc_listener->set_name("reverse_connection_listener");
    auto* rc_sock = rc_listener->mutable_address()->mutable_socket_address();
    // rc://<node>:<cluster>:<tenant>@<target_cluster>:<count>
    rc_sock->set_address(
        "rc://integration-test-node:integration-test-cluster:integration-test-tenant@"
        "upstream_cluster:1");
    rc_sock->set_port_value(0);
    // Tell Envoy to use our custom resolver for rc:// scheme.
    rc_sock->set_resolver_name("envoy.resolvers.reverse_connection");
    // Minimal filter chain; echo is fine since accept() returns a connected socket.
    auto* rc_chain = rc_listener->add_filter_chains();
    auto* echo_filter = rc_chain->add_filters();
    echo_filter->set_name("envoy.filters.network.echo");
    auto* echo_any = echo_filter->mutable_typed_config();
    echo_any->set_type_url("type.googleapis.com/envoy.extensions.filters.network.echo.v3.Echo");

    // Define the upstream cluster that points to the upstream_listener via internal address.
    auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
    cluster->set_name("upstream_cluster");
    cluster->set_type(envoy::config::cluster::v3::Cluster::STATIC);
    cluster->mutable_load_assignment()->set_cluster_name("upstream_cluster");
    // Configure transport socket for internal upstream connections.
    auto* ts = cluster->mutable_transport_socket();
    ts->set_name("envoy.transport_sockets.internal_upstream");
    envoy::extensions::transport_sockets::internal_upstream::v3::InternalUpstreamTransport ts_cfg;
    // Wrap a raw_buffer transport socket as the underlying transport.
    auto* inner_ts = ts_cfg.mutable_transport_socket();
    inner_ts->set_name("envoy.transport_sockets.raw_buffer");
    Protobuf::Any* inner_any = inner_ts->mutable_typed_config();
    inner_any->set_type_url(
        "type.googleapis.com/envoy.extensions.transport_sockets.raw_buffer.v3.RawBuffer");
    Protobuf::Any* ts_any = ts->mutable_typed_config();
    ts_any->PackFrom(ts_cfg);

    auto* locality = cluster->mutable_load_assignment()->add_endpoints();
    auto* lb_endpoint = locality->add_lb_endpoints();
    auto* endpoint = lb_endpoint->mutable_endpoint();
    auto* ep_addr = endpoint->mutable_address()->mutable_envoy_internal_address();
    ep_addr->set_server_listener_name("upstream_listener");
    ep_addr->set_endpoint_id("rt_endpoint");
  });

  BaseIntegrationTest::initialize();

  // Wait for the upstream side to record at least one accepted connection for the node and cluster.
  // ReverseTunnelAcceptorExtension publishes gauges with names:
  //   reverse_connections.nodes.<node_id>
  //   reverse_connections.clusters.<cluster_id>
  test_server_->waitForGaugeEq("reverse_connections.nodes.integration-test-node", 1);
  test_server_->waitForGaugeEq("reverse_connections.clusters.integration-test-cluster", 1);
}

} // namespace
} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
