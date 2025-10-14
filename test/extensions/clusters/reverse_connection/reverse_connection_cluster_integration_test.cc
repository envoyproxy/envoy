#include <thread>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/clusters/reverse_connection/v3/reverse_connection.pb.h"
#include "envoy/extensions/filters/http/lua/v3/lua.pb.h"
#include "envoy/extensions/filters/network/reverse_tunnel/v3/reverse_tunnel.pb.h"
#include "envoy/extensions/transport_sockets/internal_upstream/v3/internal_upstream.pb.h"
#include "envoy/extensions/upstreams/http/v3/http_protocol_options.pb.h"

#include "source/common/protobuf/protobuf.h"

#include "test/integration/http_integration.h"
#include "test/integration/utility.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace ReverseConnection {
namespace {

class ReverseConnectionClusterIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  ReverseConnectionClusterIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam(), ConfigHelper::httpProxyConfig()) {}

  void initialize() override {
    // Set up one fake upstream for the final destination service.
    setUpstreamCount(1);

    // Add bootstrap extensions required for reverse tunnel functionality.
    config_helper_.addBootstrapExtension(R"EOF(
name: envoy.bootstrap.reverse_tunnel.upstream_socket_interface
typed_config:
  "@type": type.googleapis.com/envoy.extensions.bootstrap.reverse_tunnel.upstream_socket_interface.v3.UpstreamReverseConnectionSocketInterface
  enable_detailed_stats: true
)EOF");

    config_helper_.addBootstrapExtension(R"EOF(
name: envoy.bootstrap.reverse_tunnel.downstream_socket_interface
typed_config:
  "@type": type.googleapis.com/envoy.extensions.bootstrap.reverse_tunnel.downstream_socket_interface.v3.DownstreamReverseConnectionSocketInterface
  enable_detailed_stats: true
)EOF");

    // Call parent initialize to complete setup.
    HttpIntegrationTest::initialize();
  }

protected:
  // Set log level to debug for this test class.
  LogLevelSetter log_level_setter_ = LogLevelSetter(spdlog::level::trace);
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ReverseConnectionClusterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// End-to-end reverse connection cluster test where:
// 1. A listener with reverse_tunnel filter accepts reverse tunnel connections
// 2. A reverse connection cluster initiates connections to that listener
// 3. HTTP traffic flows through the reverse tunnel to a fake upstream
TEST_P(ReverseConnectionClusterIntegrationTest, EndToEndReverseTunnelWithCluster) {
  DISABLE_IF_ADMIN_DISABLED; // Test requires admin interface for cleanup.

  // Use a deterministic port for tunnel listener.
  const uint32_t tunnel_listener_port =
      GetParam() == Network::Address::IpVersion::v4 ? 15000 : 15001;
  const std::string loopback_addr =
      GetParam() == Network::Address::IpVersion::v4 ? "127.0.0.1" : "::1";

  // Configure the full reverse tunnel flow with cluster.
  config_helper_.addConfigModifier([tunnel_listener_port, loopback_addr](
                                       envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Clear existing listeners, but keep cluster_0 which will be auto-populated with fake_upstreams_[0].
    bootstrap.mutable_static_resources()->clear_listeners();

    // Ensure admin interface is configured.
    if (!bootstrap.has_admin()) {
      auto* admin = bootstrap.mutable_admin();
      auto* admin_address = admin->mutable_address()->mutable_socket_address();
      admin_address->set_address(loopback_addr);
      admin_address->set_port_value(0); // Use ephemeral port
    }

    // Create the upstream tunnel listener that accepts reverse tunnel handshake connections.
    auto* tunnel_listener = bootstrap.mutable_static_resources()->add_listeners();
    tunnel_listener->set_name("tunnel_listener");
    tunnel_listener->mutable_address()->mutable_socket_address()->set_address(loopback_addr);
    tunnel_listener->mutable_address()->mutable_socket_address()->set_port_value(
        tunnel_listener_port);

    auto* tunnel_chain = tunnel_listener->add_filter_chains();
    auto* rt_filter = tunnel_chain->add_filters();
    rt_filter->set_name("envoy.filters.network.reverse_tunnel");

    // Configure the reverse tunnel filter with a high ping interval to avoid timeout.
    envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel rt_config;
    rt_config.mutable_ping_interval()->set_seconds(300);
    rt_config.set_auto_close_connections(false);
    rt_config.set_request_path("/reverse_connections/request");
    rt_config.set_request_method(envoy::config::core::v3::GET);
    rt_filter->mutable_typed_config()->PackFrom(rt_config);

    // Create the upstream egress listener that accepts client HTTP connections and routes
    // traffic to the reverse connection cluster.
    auto* egress_listener = bootstrap.mutable_static_resources()->add_listeners();
    egress_listener->set_name("egress_listener");
    auto* egress_address = egress_listener->mutable_address()->mutable_socket_address();
    egress_address->set_address(loopback_addr);
    egress_address->set_port_value(0); // Use ephemeral port assigned by OS

    auto* egress_chain = egress_listener->add_filter_chains();
    auto* egress_hcm_filter = egress_chain->add_filters();
    egress_hcm_filter->set_name("envoy.filters.network.http_connection_manager");

    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager
        egress_hcm;
    egress_hcm.set_stat_prefix("egress_http");
    egress_hcm.set_codec_type(
        envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
            AUTO);
    
    auto* egress_route_config = egress_hcm.mutable_route_config();
    egress_route_config->set_name("local_route");
    auto* egress_virtual_host = egress_route_config->add_virtual_hosts();
    egress_virtual_host->set_name("backend");
    egress_virtual_host->add_domains("*");
    
    auto* egress_route = egress_virtual_host->add_routes();
    egress_route->mutable_match()->set_prefix("/");
    egress_route->mutable_route()->set_cluster("reverse_connection_cluster");

    // Add Lua filter to compute x-computed-host-id from request headers.
    auto* lua_filter = egress_hcm.add_http_filters();
    lua_filter->set_name("envoy.filters.http.lua");
    envoy::extensions::filters::http::lua::v3::Lua lua_config;
    lua_config.set_inline_code(R"(
      function envoy_on_request(request_handle)
        local headers = request_handle:headers()
        local node_id = headers:get("x-node-id")
        local cluster_id = headers:get("x-cluster-id")

        local host_id = ""

        -- Priority 1: x-node-id header
        if node_id then
          host_id = node_id
        -- Priority 2: x-cluster-id header
        elseif cluster_id then
          host_id = cluster_id
        else
          -- Default to test-node-id if no headers provided
          host_id = "test-node-id"
        end

        -- Set the computed host ID for the reverse connection cluster
        headers:add("x-computed-host-id", host_id)
      end
    )");
    lua_filter->mutable_typed_config()->PackFrom(lua_config);

    auto* egress_router = egress_hcm.add_http_filters();
    egress_router->set_name("envoy.filters.http.router");
    egress_router->mutable_typed_config()->PackFrom(
        envoy::extensions::filters::http::router::v3::Router());

    egress_hcm_filter->mutable_typed_config()->PackFrom(egress_hcm);

    // Create the upstream reverse connection cluster that looks up cached sockets.
    auto* rc_cluster = bootstrap.mutable_static_resources()->add_clusters();
    rc_cluster->set_name("reverse_connection_cluster");
    rc_cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);
    rc_cluster->mutable_connect_timeout()->set_seconds(5);

    // Configure the cluster as a reverse connection cluster type.
    auto* cluster_type = rc_cluster->mutable_cluster_type();
    cluster_type->set_name("envoy.clusters.reverse_connection");
    
    envoy::extensions::clusters::reverse_connection::v3::ReverseConnectionClusterConfig rc_config;
    // The host_id_format specifies how to extract the host identifier from the request.
    // This should match the node_id used in the reverse tunnel handshake.
    rc_config.set_host_id_format("%REQ(x-computed-host-id)%");
    rc_config.mutable_cleanup_interval()->set_seconds(60);
    cluster_type->mutable_typed_config()->PackFrom(rc_config);

    // Configure HTTP/2 protocol for the reverse connection cluster.
    envoy::extensions::upstreams::http::v3::HttpProtocolOptions http_options;
    http_options.mutable_explicit_http_config()->mutable_http2_protocol_options();
    (*rc_cluster->mutable_typed_extension_protocol_options())
        ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
            .PackFrom(http_options);

    // Create the downstream initiating listener that establishes reverse tunnel connections
    // using the rc:// address format.
    auto* init_listener = bootstrap.mutable_static_resources()->add_listeners();
    init_listener->set_name("reverse_conn_listener");
    init_listener->mutable_listener_filters_timeout()->set_seconds(0);
    
    // Use rc:// address format to encode reverse connection metadata.
    // Format: rc://node_id:cluster_id:tenant_id@upstream_cluster_name:connection_count
    auto* init_address = init_listener->mutable_address()->mutable_socket_address();
    init_address->set_address("rc://test-node-id:test-cluster-id:test-tenant-id@tunnel_cluster:1");
    init_address->set_port_value(0);
    init_address->set_resolver_name("envoy.resolvers.reverse_connection");

    // Add a simple HTTP connection manager to the initiating listener that routes
    // traffic coming back through the reverse tunnel to the fake upstream.
    auto* init_chain = init_listener->add_filter_chains();
    auto* init_hcm_filter = init_chain->add_filters();
    init_hcm_filter->set_name("envoy.filters.network.http_connection_manager");

    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager
        init_hcm;
    init_hcm.set_stat_prefix("reverse_conn_initiator");
    init_hcm.set_codec_type(
        envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
            AUTO);

    auto* init_route_config = init_hcm.mutable_route_config();
    init_route_config->set_name("local_route");
    auto* init_virtual_host = init_route_config->add_virtual_hosts();
    init_virtual_host->set_name("backend");
    init_virtual_host->add_domains("*");

    auto* init_route = init_virtual_host->add_routes();
    init_route->mutable_match()->set_prefix("/");
    init_route->mutable_route()->set_cluster("cluster_0");

    auto* init_router = init_hcm.add_http_filters();
    init_router->set_name("envoy.filters.http.router");
    init_router->mutable_typed_config()->PackFrom(
        envoy::extensions::filters::http::router::v3::Router());

    init_hcm_filter->mutable_typed_config()->PackFrom(init_hcm);

    // Create the tunnel cluster that points to the upstream tunnel listener.
    // This cluster is used by the rc:// address to establish reverse tunnel connections.
    auto* tunnel_cluster = bootstrap.mutable_static_resources()->add_clusters();
    tunnel_cluster->set_name("tunnel_cluster");
    tunnel_cluster->set_type(envoy::config::cluster::v3::Cluster::STATIC);
    tunnel_cluster->mutable_connect_timeout()->set_seconds(5);
    tunnel_cluster->mutable_load_assignment()->set_cluster_name("tunnel_cluster");

    auto* tunnel_locality = tunnel_cluster->mutable_load_assignment()->add_endpoints();
    auto* tunnel_lb_endpoint = tunnel_locality->add_lb_endpoints();
    auto* tunnel_endpoint = tunnel_lb_endpoint->mutable_endpoint();
    auto* tunnel_addr = tunnel_endpoint->mutable_address()->mutable_socket_address();
    tunnel_addr->set_address(loopback_addr);
    tunnel_addr->set_port_value(tunnel_listener_port);

    // Note: cluster_0 will be automatically populated by the test framework with
    // fake_upstreams_[0] endpoints. No need to create it manually.
  });

  // Initialize the test server.
  initialize();

  // Register listener ports in the order they were created.
  // First, the tunnel listener, then the egress listener.
  registerTestServerPorts({"tunnel_listener", "egress_listener"});

  ENVOY_LOG_MISC(info, "Waiting for reverse tunnel connections to be established.");
  
  // Wait for reverse tunnel to establish.
  test_server_->waitForCounterGe("reverse_tunnel.handshake.accepted", 1,
                                 std::chrono::milliseconds(5000));

  // Verify reverse tunnel stats.
  test_server_->waitForGaugeGe("reverse_tunnel_acceptor.nodes.test-node-id", 1);
  test_server_->waitForGaugeGe("reverse_tunnel_acceptor.clusters.test-cluster-id", 1);

  ENVOY_LOG_MISC(info, "Reverse tunnel established. Sending HTTP request through tunnel.");

  // Now send an HTTP request through the egress listener which routes to the reverse
  // connection cluster.
  codec_client_ = makeHttpConnection(lookupPort("egress_listener"));

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  
  // Wait for the request to arrive at the fake upstream through the reverse tunnel.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Verify the request made it through.
  EXPECT_EQ(upstream_request_->headers().getPathValue(), "/test/long/url");
  EXPECT_EQ(upstream_request_->headers().getMethodValue(), "GET");

  // Send response back through the tunnel.
  upstream_request_->encodeHeaders(default_response_headers_, true);

  // Verify the response made it back to the client.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  ENVOY_LOG_MISC(info, "End-to-end request/response through reverse tunnel successful.");

  // Cleanup.
  cleanupUpstreamAndDownstream();
}

} // namespace
} // namespace ReverseConnection
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy

