
#include <thread>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/clusters/reverse_connection/v3/reverse_connection.pb.h"
#include "envoy/extensions/filters/http/lua/v3/lua.pb.h"
#include "envoy/extensions/filters/network/reverse_tunnel/v3/reverse_tunnel.pb.h"
#include "envoy/extensions/transport_sockets/internal_upstream/v3/internal_upstream.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/extensions/upstreams/http/v3/http_protocol_options.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/protobuf/protobuf.h"

#include "test/integration/http_integration.h"
#include "test/integration/utility.h"
#include "test/test_common/environment.h"
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
      : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam(), ConfigHelper::httpProxyConfig()) {}

  ~ReverseConnectionClusterIntegrationTest() override {
    // Ensure proper cleanup even if test fails/times out.
    if (test_server_) {
      // Best-effort cleanup of listeners to ensure reverse connection sockets are cleaned up before
      // workers.
      BufferingStreamDecoderPtr drain_response = IntegrationUtil::makeSingleRequest(
          lookupPort("admin"), "POST", "/drain_listeners", "", Http::CodecType::HTTP1, GetParam());
      EXPECT_TRUE(drain_response->complete());
      EXPECT_EQ("200", drain_response->headers().getStatusValue());
    }
  }

  void initialize() override {
    // Set up one fake upstream for the final destination service.
    setUpstreamCount(1);

    // Configure HTTP/2 for upstream to support concurrent requests on a single connection.
    setUpstreamProtocol(Http::CodecType::HTTP2);

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
  LogLevelSetter log_level_setter_ = LogLevelSetter(spdlog::level::debug);

  // LDS support for dynamic listener management.
  struct FakeUpstreamInfo {
    FakeHttpConnectionPtr connection_;
    FakeStreamPtr stream_;
  };

  FakeUpstreamInfo lds_upstream_info_;

  void createLdsStream() {
    if (lds_upstream_info_.connection_ == nullptr) {
      ASSERT_TRUE(fake_upstreams_.back()->waitForHttpConnection(*dispatcher_,
                                                                lds_upstream_info_.connection_));
    }
    ASSERT_TRUE(
        lds_upstream_info_.connection_->waitForNewStream(*dispatcher_, lds_upstream_info_.stream_));
    lds_upstream_info_.stream_->startGrpcStream();
  }

  void sendLdsResponse(const std::vector<envoy::config::listener::v3::Listener>& listener_configs,
                       const std::string& version) {
    envoy::service::discovery::v3::DiscoveryResponse response;
    response.set_version_info(version);
    response.set_type_url(Config::TestTypeUrl::get().Listener);
    for (const auto& listener_config : listener_configs) {
      response.add_resources()->PackFrom(listener_config);
    }
    ASSERT_NE(nullptr, lds_upstream_info_.stream_);
    lds_upstream_info_.stream_->sendGrpcMessage(response);
  }

  // Helper function to configure reverse tunnel setup.
  using TunnelClusterModifier = std::function<void(envoy::config::cluster::v3::Cluster*)>;
  using TunnelListenerModifier = std::function<void(envoy::config::listener::v3::FilterChain*)>;

  void configureReverseTunnelSetup(envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                                   const std::string& loopback_addr, uint32_t tunnel_listener_port,
                                   const std::string& node_id = "test-node-id",
                                   const std::string& cluster_id = "test-cluster-id",
                                   const std::string& tenant_id = "test-tenant-id",
                                   TunnelClusterModifier tunnel_cluster_modifier = nullptr,
                                   TunnelListenerModifier tunnel_listener_modifier = nullptr) {

    // Clear existing listeners, but keep cluster_0 which will be auto-populated with
    // fake_upstreams_[0].
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

    // Allow caller to modify filter chain (e.g., add TLS transport socket).
    if (tunnel_listener_modifier) {
      tunnel_listener_modifier(tunnel_chain);
    }

    auto* rt_filter = tunnel_chain->add_filters();
    rt_filter->set_name("envoy.filters.network.reverse_tunnel");

    // Configure the reverse tunnel filter.
    envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel rt_config;
    rt_config.mutable_ping_interval()->set_seconds(2);
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
    egress_address->set_port_value(0); // Use ephemeral port assigned by OS.

    auto* egress_chain = egress_listener->add_filter_chains();
    auto* egress_hcm_filter = egress_chain->add_filters();
    egress_hcm_filter->set_name("envoy.filters.network.http_connection_manager");

    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager
        egress_hcm;
    egress_hcm.set_stat_prefix("egress_http");
    egress_hcm.set_codec_type(envoy::extensions::filters::network::http_connection_manager::v3::
                                  HttpConnectionManager::AUTO);

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
    lua_config.mutable_default_source_code()->mutable_inline_string()->assign(fmt::format(R"(
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
          -- Default to {{}} if no headers provided
          host_id = "{}"
        end

        -- Set the computed host ID for the reverse connection cluster
        headers:add("x-computed-host-id", host_id)
      end
    )",
                                                                                          node_id));
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
    init_address->set_address(
        fmt::format("rc://{}:{}:{}@tunnel_cluster:1", node_id, cluster_id, tenant_id));
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
    init_hcm.set_codec_type(envoy::extensions::filters::network::http_connection_manager::v3::
                                HttpConnectionManager::AUTO);

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

    // Allow caller to modify tunnel cluster (e.g., add TLS transport socket)
    if (tunnel_cluster_modifier) {
      tunnel_cluster_modifier(tunnel_cluster);
    }
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ReverseConnectionClusterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// End-to-end reverse connection cluster test where:
// 1. A listener with reverse_tunnel filter accepts reverse tunnel connections.
// 2. A reverse connection cluster initiates connections to that listener.
// 3. HTTP traffic flows through the reverse tunnel to a fake upstream.
TEST_P(ReverseConnectionClusterIntegrationTest, EndToEndReverseTunnelTest) {
  DISABLE_IF_ADMIN_DISABLED; // Test requires admin interface for cleanup.

  // Use a deterministic port for tunnel listener.
  const uint32_t tunnel_listener_port =
      GetParam() == Network::Address::IpVersion::v4 ? 15000 : 15001;
  const std::string loopback_addr =
      GetParam() == Network::Address::IpVersion::v4 ? "127.0.0.1" : "::1";

  // Configure the full reverse tunnel flow with cluster using helper.
  config_helper_.addConfigModifier([this, tunnel_listener_port, loopback_addr](
                                       envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    configureReverseTunnelSetup(bootstrap, loopback_addr, tunnel_listener_port);
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

  // Verify no handshake errors occurred.
  EXPECT_EQ(test_server_->counter("reverse_tunnel.handshake.parse_error")->value(), 0);
  EXPECT_EQ(test_server_->counter("reverse_tunnel.handshake.rejected")->value(), 0);
  EXPECT_EQ(test_server_->counter("reverse_tunnel.handshake.validation_failed")->value(), 0);

  // Verify downstream initiator stats (with detailed stats enabled).
  ENVOY_LOG_MISC(info, "Verifying downstream reverse tunnel initiator stats.");
  const std::string tunnel_address = fmt::format("{}:{}", loopback_addr, tunnel_listener_port);

  // Wait for initiator connection stats - the stat name includes the actual address:port
  // Format: reverse_tunnel_initiator.host.<address>:<port>.connected
  // Note: IPv6 addresses use bracket notation [::1]:port in stat names
  const std::string formatted_tunnel_address =
      (GetParam() == Network::Address::IpVersion::v6)
          ? fmt::format("[{}]:{}", loopback_addr, tunnel_listener_port)
          : tunnel_address;
  const std::string initiator_host_stat =
      fmt::format("reverse_tunnel_initiator.host.{}.connected", formatted_tunnel_address);
  test_server_->waitForGaugeGe(initiator_host_stat, 1, std::chrono::milliseconds(2000));

  // Verify cluster-level initiator stats.
  test_server_->waitForGaugeGe("reverse_tunnel_initiator.cluster.tunnel_cluster.connected", 1);

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

  // Verify cluster stats for the reverse connection cluster.
  ENVOY_LOG_MISC(info, "Verifying reverse connection cluster stats.");
  test_server_->waitForCounterGe("cluster.reverse_connection_cluster.upstream_cx_total", 1);
  test_server_->waitForCounterGe("cluster.reverse_connection_cluster.upstream_rq_total", 1);
  test_server_->waitForCounterGe("cluster.reverse_connection_cluster.upstream_rq_completed", 1);
  EXPECT_EQ(
      test_server_->counter("cluster.reverse_connection_cluster.upstream_cx_connect_fail")->value(),
      0);

  // Test concurrent requests with different headers using the established tunnel.
  ENVOY_LOG_MISC(info, "Testing concurrent requests with different headers.");

  // Create multiple concurrent requests using various tunnel identifiers.
  std::vector<Http::RequestEncoder*> encoders;
  std::vector<IntegrationStreamDecoderPtr> responses;
  std::vector<Http::TestRequestHeaderMapImpl> test_headers;

  // Request 1: Use default node-id (test-node-id via Lua script default).
  test_headers.push_back(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/test/path1"}, {":scheme", "http"}, {":authority", "host"}});

  // Request 2: Explicitly specify x-node-id header with test-node-id.
  test_headers.push_back(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                        {":path", "/test/path2"},
                                                        {":scheme", "http"},
                                                        {":authority", "host"},
                                                        {"x-node-id", "test-node-id"}});

  // Request 3: Use x-cluster-id header with test-cluster-id.
  test_headers.push_back(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                        {":path", "/test/path3"},
                                                        {":scheme", "http"},
                                                        {":authority", "host"},
                                                        {"x-cluster-id", "test-cluster-id"}});

  // Request 4: Both x-node-id and x-cluster-id (x-node-id takes precedence per Lua).
  test_headers.push_back(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                        {":path", "/test/path4"},
                                                        {":scheme", "http"},
                                                        {":authority", "host"},
                                                        {"x-node-id", "test-node-id"},
                                                        {"x-cluster-id", "test-cluster-id"}});

  // Request 5: Another request with default routing to verify tunnel reuse.
  test_headers.push_back(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/test/path5"}, {":scheme", "http"}, {":authority", "host"}});

  // Send all requests concurrently.
  ENVOY_LOG_MISC(info, "Sending {} concurrent requests.", test_headers.size());
  for (size_t i = 0; i < test_headers.size(); i++) {
    auto encoder_decoder = codec_client_->startRequest(test_headers[i]);
    encoders.push_back(&encoder_decoder.first);
    responses.push_back(std::move(encoder_decoder.second));
    codec_client_->sendData(*encoders[i], 0, true);
  }

  // Collect all upstream streams.
  ENVOY_LOG_MISC(info, "Collecting {} upstream streams.", test_headers.size());
  std::vector<FakeStreamPtr> upstream_streams;
  for (size_t i = 0; i < test_headers.size(); i++) {
    FakeStreamPtr stream;
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, stream));
    ASSERT_TRUE(stream->waitForEndStream(*dispatcher_));
    upstream_streams.push_back(std::move(stream));
  }

  // Verify all upstream streams received expected paths and respond to all.
  ENVOY_LOG_MISC(info, "Responding to {} upstream streams.", upstream_streams.size());
  for (size_t i = 0; i < upstream_streams.size(); i++) {
    const auto actual_path = upstream_streams[i]->headers().getPathValue();
    ENVOY_LOG_MISC(info, "Upstream stream {} received request with path: {}", i, actual_path);

    // Verify it's one of our expected test paths.
    bool path_matched = false;
    for (size_t j = 1; j <= 5; j++) {
      if (actual_path == fmt::format("/test/path{}", j)) {
        path_matched = true;
        break;
      }
    }
    EXPECT_TRUE(path_matched) << "Unexpected path: " << actual_path;

    // Send response.
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    upstream_streams[i]->encodeHeaders(response_headers, true);
  }

  // Wait for all responses to complete.
  ENVOY_LOG_MISC(info, "Waiting for all {} responses to complete.", responses.size());
  for (size_t i = 0; i < responses.size(); i++) {
    ASSERT_TRUE(responses[i]->waitForEndStream());
    EXPECT_TRUE(responses[i]->complete());
    EXPECT_EQ("200", responses[i]->headers().getStatusValue());
  }

  ENVOY_LOG_MISC(info, "All {} concurrent requests successfully completed.", responses.size());

  // Verify updated cluster stats after concurrent requests.
  ENVOY_LOG_MISC(info, "Verifying updated stats after concurrent requests.");
  test_server_->waitForCounterGe("cluster.reverse_connection_cluster.upstream_rq_total",
                                 6); // 1 initial + 5 concurrent
  test_server_->waitForCounterGe("cluster.reverse_connection_cluster.upstream_rq_completed", 6);

  // Verify that all requests routed through the existing reverse tunnel.
  // Since all requests use test-node-id or test-cluster-id (which both map to the same tunnel),
  // they all successfully use the established connection.
  test_server_->waitForCounterEq("reverse_tunnel.handshake.accepted", 1);
  ENVOY_LOG_MISC(info,
                 "All concurrent requests successfully routed through single established tunnel.");

  ENVOY_LOG_MISC(info, "All tests completed successfully.");

  // Cleanup connections before server shutdown.
  cleanupUpstreamAndDownstream();

  // Drain listeners via admin interface to ensure proper cleanup of reverse connection sockets
  // before workers are destroyed.
  BufferingStreamDecoderPtr drain_response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/drain_listeners", "", Http::CodecType::HTTP1, GetParam());
  EXPECT_TRUE(drain_response->complete());
  EXPECT_EQ("200", drain_response->headers().getStatusValue());

  // Wait for listeners to be fully stopped before test cleanup.
  test_server_->waitForCounterEq("listener_manager.listener_stopped", 3,
                                 std::chrono::milliseconds(5000));
}

// End-to-end reverse connection cluster test with mTLS.
TEST_P(ReverseConnectionClusterIntegrationTest, EndToEndReverseTunnelTestWithMutualTLS) {
  DISABLE_IF_ADMIN_DISABLED; // Test requires admin interface for cleanup.

  // Use a deterministic port for tunnel listener.
  const uint32_t tunnel_listener_port =
      GetParam() == Network::Address::IpVersion::v4 ? 15000 : 15001;
  const std::string loopback_addr =
      GetParam() == Network::Address::IpVersion::v4 ? "127.0.0.1" : "::1";

  const std::string rundir = TestEnvironment::runfilesDirectory();

  // Configure the full reverse tunnel flow with mTLS.
  config_helper_.addConfigModifier([this, tunnel_listener_port, loopback_addr,
                                    rundir](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Define modifiers for TLS configuration
    auto tunnel_listener_modifier = [&rundir](envoy::config::listener::v3::FilterChain* chain) {
      // Configure downstream TLS context (server side) for tunnel_listener on responder envoy.
      auto* transport_socket = chain->mutable_transport_socket();
      transport_socket->set_name("envoy.transport_sockets.tls");

      envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;

      // Server certificates.
      auto* tls_cert = tls_context.mutable_common_tls_context()->add_tls_certificates();
      tls_cert->mutable_certificate_chain()->set_filename(
          rundir + "/test/config/integration/certs/servercert.pem");
      tls_cert->mutable_private_key()->set_filename(rundir +
                                                    "/test/config/integration/certs/serverkey.pem");

      // Require client certificate for mTLS.
      tls_context.mutable_require_client_certificate()->set_value(true);

      // Trusted CA list for validating client certificates.
      tls_context.mutable_common_tls_context()
          ->mutable_validation_context()
          ->mutable_trusted_ca()
          ->set_filename(rundir + "/test/config/integration/certs/cacert.pem");

      tls_context.mutable_common_tls_context()->add_alpn_protocols("h2");

      transport_socket->mutable_typed_config()->PackFrom(tls_context);
    };

    auto tunnel_cluster_modifier = [&rundir](envoy::config::cluster::v3::Cluster* cluster) {
      // Configure upstream TLS context for tunnel_cluster on initiator envoy.
      auto* transport_socket = cluster->mutable_transport_socket();
      transport_socket->set_name("envoy.transport_sockets.tls");

      envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;

      // Client certificate for mTLS.
      auto* tls_cert = tls_context.mutable_common_tls_context()->add_tls_certificates();
      tls_cert->mutable_certificate_chain()->set_filename(
          rundir + "/test/config/integration/certs/clientcert.pem");
      tls_cert->mutable_private_key()->set_filename(rundir +
                                                    "/test/config/integration/certs/clientkey.pem");

      // Trusted CA list for validating server certificate.
      tls_context.mutable_common_tls_context()
          ->mutable_validation_context()
          ->mutable_trusted_ca()
          ->set_filename(rundir + "/test/config/integration/certs/cacert.pem");

      //
      tls_context.mutable_common_tls_context()->add_alpn_protocols("h2");

      transport_socket->mutable_typed_config()->PackFrom(tls_context);
    };

    configureReverseTunnelSetup(bootstrap, loopback_addr, tunnel_listener_port, "test-node-id",
                                "test-cluster-id", "test-tenant-id", tunnel_cluster_modifier,
                                tunnel_listener_modifier);
  });

  // Initialize the test server.
  initialize();

  // Register listener ports in the order they were created.
  registerTestServerPorts({"tunnel_listener", "egress_listener"});

  ENVOY_LOG_MISC(info, "Waiting for mTLS reverse tunnel connections to be established.");

  // Wait for reverse tunnel to establish with mTLS.
  test_server_->waitForCounterGe("reverse_tunnel.handshake.accepted", 1,
                                 std::chrono::milliseconds(5000));

  // Verify reverse tunnel stats.
  test_server_->waitForGaugeGe("reverse_tunnel_acceptor.nodes.test-node-id", 1);
  test_server_->waitForGaugeGe("reverse_tunnel_acceptor.clusters.test-cluster-id", 1);

  // Verify no handshake errors occurred.
  EXPECT_EQ(test_server_->counter("reverse_tunnel.handshake.parse_error")->value(), 0);
  EXPECT_EQ(test_server_->counter("reverse_tunnel.handshake.rejected")->value(), 0);
  EXPECT_EQ(test_server_->counter("reverse_tunnel.handshake.validation_failed")->value(), 0);

  // Verify downstream initiator stats.
  ENVOY_LOG_MISC(info, "Verifying downstream reverse tunnel initiator stats.");
  const std::string tunnel_address = fmt::format("{}:{}", loopback_addr, tunnel_listener_port);
  // Note: IPv6 addresses use bracket notation [::1]:port in stat names
  const std::string formatted_tunnel_address =
      (GetParam() == Network::Address::IpVersion::v6)
          ? fmt::format("[{}]:{}", loopback_addr, tunnel_listener_port)
          : tunnel_address;
  const std::string initiator_host_stat =
      fmt::format("reverse_tunnel_initiator.host.{}.connected", formatted_tunnel_address);
  test_server_->waitForGaugeGe(initiator_host_stat, 1, std::chrono::milliseconds(1000));
  test_server_->waitForGaugeGe("reverse_tunnel_initiator.cluster.tunnel_cluster.connected", 1);

  // Give a small delay for pings to occur.
  test_server_->waitForCounterGe("reverse_tunnel.handshake.accepted", 1,
                                 std::chrono::milliseconds(1000));

  ENVOY_LOG_MISC(info, "Sending HTTP request through mTLS tunnel.");

  // Send an HTTP request through the egress listener which routes to the reverse
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

  ENVOY_LOG_MISC(info, "End-to-end request/response through mTLS reverse tunnel successful.");

  // Verify cluster stats for the reverse connection cluster.
  ENVOY_LOG_MISC(info, "Verifying reverse connection cluster stats.");
  test_server_->waitForCounterGe("cluster.reverse_connection_cluster.upstream_cx_total", 1);
  test_server_->waitForCounterGe("cluster.reverse_connection_cluster.upstream_rq_total", 1);
  test_server_->waitForCounterGe("cluster.reverse_connection_cluster.upstream_rq_completed", 1);
  EXPECT_EQ(
      test_server_->counter("cluster.reverse_connection_cluster.upstream_cx_connect_fail")->value(),
      0);

  ENVOY_LOG_MISC(info, "mTLS reverse tunnel test completed successfully.");

  // Cleanup connections before server shutdown.
  cleanupUpstreamAndDownstream();

  // Drain listeners via admin interface to ensure proper cleanup of reverse connection sockets
  // before workers are destroyed.
  BufferingStreamDecoderPtr drain_response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/drain_listeners", "", Http::CodecType::HTTP1, GetParam());
  EXPECT_TRUE(drain_response->complete());
  EXPECT_EQ("200", drain_response->headers().getStatusValue());

  // Wait for listeners to be fully stopped before test cleanup.
  test_server_->waitForCounterEq("listener_manager.listener_stopped", 3,
                                 std::chrono::milliseconds(5000));
}

// Test resilience when an initiator node goes down and comes back up.
// We use LDS to dynamically remove/add initiator listeners for node-1 while keeping node-2 active.
TEST_P(ReverseConnectionClusterIntegrationTest, ReverseTunnelResiliencyTest) {
  DISABLE_IF_ADMIN_DISABLED;

  const uint32_t cloud1_port = GetParam() == Network::Address::IpVersion::v4 ? 15000 : 15001;
  const uint32_t cloud2_port = GetParam() == Network::Address::IpVersion::v4 ? 15002 : 15003;
  const std::string loopback_addr =
      GetParam() == Network::Address::IpVersion::v4 ? "127.0.0.1" : "::1";

  // Store listener configs for LDS updates.
  envoy::config::listener::v3::Listener node1_cloud1_config;
  envoy::config::listener::v3::Listener node1_cloud2_config;
  envoy::config::listener::v3::Listener node2_cloud1_config;
  envoy::config::listener::v3::Listener node2_cloud2_config;

  // Configure with LDS for dynamic initiator listener management.
  config_helper_.addConfigModifier([cloud1_port, cloud2_port, loopback_addr, &node1_cloud1_config,
                                    &node1_cloud2_config, &node2_cloud1_config,
                                    &node2_cloud2_config](
                                       envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    bootstrap.mutable_static_resources()->clear_listeners();

    // Configure LDS for dynamic listener management.
    auto* lds_cluster = bootstrap.mutable_static_resources()->add_clusters();
    lds_cluster->set_name("lds_cluster");
    lds_cluster->set_type(envoy::config::cluster::v3::Cluster::STATIC);
    lds_cluster->mutable_connect_timeout()->set_seconds(5);

    // Configure HTTP/2 protocol using modern API
    envoy::extensions::upstreams::http::v3::HttpProtocolOptions lds_http_options;
    lds_http_options.mutable_explicit_http_config()->mutable_http2_protocol_options();
    (*lds_cluster->mutable_typed_extension_protocol_options())
        ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
            .PackFrom(lds_http_options);

    auto* lds_load_assignment = lds_cluster->mutable_load_assignment();
    lds_load_assignment->set_cluster_name("lds_cluster");
    auto* lds_endpoint = lds_load_assignment->add_endpoints()->add_lb_endpoints();
    lds_endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address(
        loopback_addr);
    lds_endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(
        0); // Will be set by fake upstream

    // Configure dynamic listener resources via LDS.
    auto* lds_config_source = bootstrap.mutable_dynamic_resources()->mutable_lds_config();
    lds_config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
    auto* lds_api_config_source = lds_config_source->mutable_api_config_source();
    lds_api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    lds_api_config_source->set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
    auto* grpc_service = lds_api_config_source->add_grpc_services();
    grpc_service->mutable_envoy_grpc()->set_cluster_name("lds_cluster");

    // Helper lambda to build an initiator listener config.
    auto build_initiator_listener = [&](envoy::config::listener::v3::Listener& listener, int node,
                                        int cloud) {
      listener.set_name(fmt::format("node_{}_to_cloud_{}", node, cloud));
      listener.mutable_listener_filters_timeout()->set_seconds(0);
      listener.set_drain_type(envoy::config::listener::v3::Listener::DEFAULT);

      auto* init_address = listener.mutable_address()->mutable_socket_address();
      init_address->set_address(
          fmt::format("rc://node-{}:test-cluster:test-tenant@tunnel_cluster_{}:1", node, cloud));
      init_address->set_port_value(0);
      init_address->set_resolver_name("envoy.resolvers.reverse_connection");

      auto* init_chain = listener.add_filter_chains();
      auto* init_hcm_filter = init_chain->add_filters();
      init_hcm_filter->set_name("envoy.filters.network.http_connection_manager");

      envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager
          init_hcm;
      init_hcm.set_stat_prefix(fmt::format("node_{}_to_cloud_{}", node, cloud));
      init_hcm.set_codec_type(envoy::extensions::filters::network::http_connection_manager::v3::
                                  HttpConnectionManager::AUTO);

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
    };

    // Build initiator listener configs.
    build_initiator_listener(node1_cloud1_config, 1, 1);
    build_initiator_listener(node1_cloud2_config, 1, 2);
    build_initiator_listener(node2_cloud1_config, 2, 1);
    build_initiator_listener(node2_cloud2_config, 2, 2);

    // Create static cloud acceptor listeners.
    for (int i = 1; i <= 2; i++) {
      auto* cloud_listener = bootstrap.mutable_static_resources()->add_listeners();
      cloud_listener->set_name(fmt::format("cloud_{}_listener", i));
      cloud_listener->mutable_address()->mutable_socket_address()->set_address(loopback_addr);
      cloud_listener->mutable_address()->mutable_socket_address()->set_port_value(
          i == 1 ? cloud1_port : cloud2_port);

      auto* cloud_chain = cloud_listener->add_filter_chains();
      auto* rt_filter = cloud_chain->add_filters();
      rt_filter->set_name("envoy.filters.network.reverse_tunnel");

      envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel rt_config;
      rt_config.mutable_ping_interval()->set_seconds(2);
      rt_config.set_auto_close_connections(false);
      rt_config.set_request_path("/reverse_connections/request");
      rt_config.set_request_method(envoy::config::core::v3::GET);
      rt_filter->mutable_typed_config()->PackFrom(rt_config);
    }

    // Create static egress listener.
    auto* egress_listener = bootstrap.mutable_static_resources()->add_listeners();
    egress_listener->set_name("egress_listener");
    auto* egress_address = egress_listener->mutable_address()->mutable_socket_address();
    egress_address->set_address(loopback_addr);
    egress_address->set_port_value(0);

    auto* egress_chain = egress_listener->add_filter_chains();
    auto* egress_hcm_filter = egress_chain->add_filters();
    egress_hcm_filter->set_name("envoy.filters.network.http_connection_manager");

    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager
        egress_hcm;
    egress_hcm.set_stat_prefix("egress_http");
    egress_hcm.set_codec_type(envoy::extensions::filters::network::http_connection_manager::v3::
                                  HttpConnectionManager::AUTO);

    auto* egress_route_config = egress_hcm.mutable_route_config();
    egress_route_config->set_name("local_route");
    auto* egress_virtual_host = egress_route_config->add_virtual_hosts();
    egress_virtual_host->set_name("backend");
    egress_virtual_host->add_domains("*");

    auto* egress_route = egress_virtual_host->add_routes();
    egress_route->mutable_match()->set_prefix("/");
    egress_route->mutable_route()->set_cluster("reverse_connection_cluster");

    // Add Lua filter for host ID computation.
    auto* lua_filter = egress_hcm.add_http_filters();
    lua_filter->set_name("envoy.filters.http.lua");
    envoy::extensions::filters::http::lua::v3::Lua lua_config;
    lua_config.mutable_default_source_code()->mutable_inline_string()->assign(R"(
          function envoy_on_request(request_handle)
            local headers = request_handle:headers()
            local node_id = headers:get("x-node-id")
            local cluster_id = headers:get("x-cluster-id")
            local host_id = node_id or cluster_id or "node-1"
            headers:add("x-computed-host-id", host_id)
          end
        )");
    lua_filter->mutable_typed_config()->PackFrom(lua_config);

    auto* egress_router = egress_hcm.add_http_filters();
    egress_router->set_name("envoy.filters.http.router");
    egress_router->mutable_typed_config()->PackFrom(
        envoy::extensions::filters::http::router::v3::Router());

    egress_hcm_filter->mutable_typed_config()->PackFrom(egress_hcm);

    // Create reverse connection cluster.
    auto* rc_cluster = bootstrap.mutable_static_resources()->add_clusters();
    rc_cluster->set_name("reverse_connection_cluster");
    rc_cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);
    rc_cluster->mutable_connect_timeout()->set_seconds(5);

    auto* cluster_type = rc_cluster->mutable_cluster_type();
    cluster_type->set_name("envoy.clusters.reverse_connection");

    envoy::extensions::clusters::reverse_connection::v3::ReverseConnectionClusterConfig rc_config;
    rc_config.set_host_id_format("%REQ(x-computed-host-id)%");
    rc_config.mutable_cleanup_interval()->set_seconds(60);
    cluster_type->mutable_typed_config()->PackFrom(rc_config);

    envoy::extensions::upstreams::http::v3::HttpProtocolOptions http_options;
    http_options.mutable_explicit_http_config()->mutable_http2_protocol_options();
    (*rc_cluster->mutable_typed_extension_protocol_options())
        ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
            .PackFrom(http_options);

    // Create 2 tunnel clusters pointing to the 2 cloud listeners.
    for (int i = 1; i <= 2; i++) {
      auto* tunnel_cluster = bootstrap.mutable_static_resources()->add_clusters();
      tunnel_cluster->set_name(fmt::format("tunnel_cluster_{}", i));
      tunnel_cluster->set_type(envoy::config::cluster::v3::Cluster::STATIC);
      tunnel_cluster->mutable_connect_timeout()->set_seconds(5);
      tunnel_cluster->mutable_load_assignment()->set_cluster_name(
          fmt::format("tunnel_cluster_{}", i));

      auto* tunnel_locality = tunnel_cluster->mutable_load_assignment()->add_endpoints();
      auto* tunnel_lb_endpoint = tunnel_locality->add_lb_endpoints();
      auto* tunnel_endpoint = tunnel_lb_endpoint->mutable_endpoint();
      auto* tunnel_addr = tunnel_endpoint->mutable_address()->mutable_socket_address();
      tunnel_addr->set_address(loopback_addr);
      tunnel_addr->set_port_value(i == 1 ? cloud1_port : cloud2_port);
    }
  });

  // Setup for LDS test.
  use_lds_ = false;
  setUpstreamCount(2); // cluster_0 + lds_cluster
  setUpstreamProtocol(Http::CodecType::HTTP2);

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

  on_server_init_function_ = [this, &node1_cloud1_config, &node1_cloud2_config,
                              &node2_cloud1_config, &node2_cloud2_config]() {
    createLdsStream();
    ENVOY_LOG_MISC(info, "Sending initial LDS with all 4 initiator listeners.");
    sendLdsResponse(
        {node1_cloud1_config, node1_cloud2_config, node2_cloud1_config, node2_cloud2_config}, "1");
  };

  setDrainTime(std::chrono::seconds(3));

  HttpIntegrationTest::initialize();

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 1);
  test_server_->waitForCounterGe("listener_manager.listener_create_success",
                                 4); // 4 initiator listeners
  test_server_->waitUntilListenersReady();
  test_server_->waitForGaugeGe("listener_manager.total_listeners_active",
                               7); // egress + 2 clouds + 4 initiators

  // Register static listener ports (cloud listeners and egress).
  registerTestServerPorts({"cloud_1_listener", "cloud_2_listener", "egress_listener"});

  ENVOY_LOG_MISC(info, "Waiting for all 4 tunnel connections to establish.");

  // Wait for all 4 tunnels (2 nodes x 2 clouds).
  test_server_->waitForCounterGe("reverse_tunnel.handshake.accepted", 4,
                                 std::chrono::milliseconds(10000));

  test_server_->waitForGaugeGe("reverse_tunnel_acceptor.nodes.node-1", 2);
  test_server_->waitForGaugeGe("reverse_tunnel_acceptor.nodes.node-2", 2);
  test_server_->waitForGaugeGe("reverse_tunnel_acceptor.clusters.test-cluster", 4);

  ENVOY_LOG_MISC(info, "All 4 tunnels established. Testing initial connectivity.");

  // Send requests through both nodes to verify initial connectivity.
  codec_client_ = makeHttpConnection(lookupPort("egress_listener"));

  Http::TestRequestHeaderMapImpl node1_headers{{":method", "GET"},
                                               {":path", "/node1-test"},
                                               {":scheme", "http"},
                                               {":authority", "host"},
                                               {"x-node-id", "node-1"}};
  auto response1 = codec_client_->makeHeaderOnlyRequest(node1_headers);

  // Wait for the HTTP/2 connection to establish.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response1->waitForEndStream());
  EXPECT_EQ("200", response1->headers().getStatusValue());

  Http::TestRequestHeaderMapImpl node2_headers{{":method", "GET"},
                                               {":path", "/node2-test"},
                                               {":scheme", "http"},
                                               {":authority", "host"},
                                               {"x-node-id", "node-2"}};
  auto response2 = codec_client_->makeHeaderOnlyRequest(node2_headers);

  // Reuse the same HTTP/2 connection for the second stream.
  FakeStreamPtr upstream_request2;
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request2));
  ASSERT_TRUE(upstream_request2->waitForEndStream(*dispatcher_));
  upstream_request2->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_EQ("200", response2->headers().getStatusValue());

  ENVOY_LOG_MISC(info, "Initial connectivity verified for both nodes.");

  // Simulate node-1 going down by removing its initiator listeners via LDS.
  ENVOY_LOG_MISC(info, "Simulating node-1 failure by removing its initiator listeners via LDS.");
  sendLdsResponse({node2_cloud1_config, node2_cloud2_config}, "2");

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 2);
  test_server_->waitForCounterGe("listener_manager.listener_removed",
                                 2); // 2 node-1 listeners removed

  // Verify stats show reduced connections (should drop from 4 to 2).
  ENVOY_LOG_MISC(info, "Verifying that node-1 connections are gone.");
  test_server_->waitForGaugeEq("reverse_tunnel_acceptor.nodes.node-1", 0);
  test_server_->waitForGaugeEq("reverse_tunnel_acceptor.nodes.node-2", 2);

  // Verify node-2 still works.
  ENVOY_LOG_MISC(info, "Verifying node-2 still works after node-1 failure.");
  Http::TestRequestHeaderMapImpl node2_verify_headers{{":method", "GET"},
                                                      {":path", "/node2-verify"},
                                                      {":scheme", "http"},
                                                      {":authority", "host"},
                                                      {"x-node-id", "node-2"}};
  auto response3 = codec_client_->makeHeaderOnlyRequest(node2_verify_headers);

  FakeStreamPtr upstream_request3;
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request3));
  ASSERT_TRUE(upstream_request3->waitForEndStream(*dispatcher_));
  upstream_request3->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response3->waitForEndStream());
  EXPECT_EQ("200", response3->headers().getStatusValue());

  // Verify node-1 requests fail (no available connection).
  ENVOY_LOG_MISC(info, "Verifying node-1 requests fail.");
  Http::TestRequestHeaderMapImpl node1_fail_headers{{":method", "GET"},
                                                    {":path", "/node1-fail"},
                                                    {":scheme", "http"},
                                                    {":authority", "host"},
                                                    {"x-node-id", "node-1"}};
  auto response4 = codec_client_->makeHeaderOnlyRequest(node1_fail_headers);
  ASSERT_TRUE(response4->waitForEndStream());
  EXPECT_EQ("503", response4->headers().getStatusValue()); // Service Unavailable

  ENVOY_LOG_MISC(info, "Node-1 failure verified.");

  // Re-add node-1 initiator listeners via LDS to simulate node recovery.
  ENVOY_LOG_MISC(info, "Simulating node-1 recovery by re-adding its initiator listeners via LDS.");
  sendLdsResponse(
      {node1_cloud1_config, node1_cloud2_config, node2_cloud1_config, node2_cloud2_config}, "3");

  test_server_->waitForCounterGe("listener_manager.lds.update_success", 3);
  test_server_->waitForCounterGe("listener_manager.listener_create_success",
                                 6); // 4 initial + 2 re-added

  // Wait for node-1 tunnels to re-establish.
  ENVOY_LOG_MISC(info, "Waiting for node-1 tunnels to re-establish.");
  test_server_->waitForCounterGe("reverse_tunnel.handshake.accepted", 6,
                                 std::chrono::milliseconds(10000)); // 4 initial + 2 reconnect

  test_server_->waitForGaugeGe("reverse_tunnel_acceptor.nodes.node-1", 2);
  test_server_->waitForGaugeEq("reverse_tunnel_acceptor.nodes.node-2", 2);

  // Verify both nodes work after recovery.
  ENVOY_LOG_MISC(info, "Verifying full connectivity restored.");
  Http::TestRequestHeaderMapImpl node1_recovery_headers{{":method", "GET"},
                                                        {":path", "/node1-recovery"},
                                                        {":scheme", "http"},
                                                        {":authority", "host"},
                                                        {"x-node-id", "node-1"}};
  auto response5 = codec_client_->makeHeaderOnlyRequest(node1_recovery_headers);

  FakeStreamPtr upstream_request5;
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request5));
  ASSERT_TRUE(upstream_request5->waitForEndStream(*dispatcher_));
  upstream_request5->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response5->waitForEndStream());
  EXPECT_EQ("200", response5->headers().getStatusValue());

  Http::TestRequestHeaderMapImpl node2_final_headers{{":method", "GET"},
                                                     {":path", "/node2-final"},
                                                     {":scheme", "http"},
                                                     {":authority", "host"},
                                                     {"x-node-id", "node-2"}};
  auto response6 = codec_client_->makeHeaderOnlyRequest(node2_final_headers);

  FakeStreamPtr upstream_request6;
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request6));
  ASSERT_TRUE(upstream_request6->waitForEndStream(*dispatcher_));
  upstream_request6->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response6->waitForEndStream());
  EXPECT_EQ("200", response6->headers().getStatusValue());

  ENVOY_LOG_MISC(info, "Initiator resilience test completed successfully!");

  // Cleanup LDS connection first to prevent race with FakeStream destruction.
  // Finish the gRPC stream to ensure no more messages are being processed.
  if (lds_upstream_info_.stream_) {
    lds_upstream_info_.stream_->finishGrpcStream(Grpc::Status::Ok);
  }
  if (lds_upstream_info_.connection_) {
    AssertionResult result = lds_upstream_info_.connection_->close();
    RELEASE_ASSERT(result, result.message());
    result = lds_upstream_info_.connection_->waitForDisconnect();
    RELEASE_ASSERT(result, result.message());
  }
  // Allow worker threads time to process the stream closure before destroying objects.
  timeSystem().advanceTimeWait(std::chrono::milliseconds(100));

  // Now reset the pointers.
  lds_upstream_info_.stream_.reset();
  lds_upstream_info_.connection_.reset();

  // Cleanup connections before server shutdown.
  cleanupUpstreamAndDownstream();

  // Drain listeners via admin interface to ensure proper cleanup of reverse connection sockets
  // before workers are destroyed.
  BufferingStreamDecoderPtr drain_response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/drain_listeners", "", Http::CodecType::HTTP1, GetParam());
  EXPECT_TRUE(drain_response->complete());
  EXPECT_EQ("200", drain_response->headers().getStatusValue());

  // Wait for listeners to be fully stopped before test cleanup.
  test_server_->waitForCounterGe("listener_manager.listener_stopped", 7,
                                 std::chrono::milliseconds(5000));
}

// Multi-worker reverse tunnel test where:
// 1. Envoy is configured with 4 workers (concurrency = 4).
// 2. Each worker initiates a reverse tunnel connection to the upstream tunnel listener.
// 3. Stats verify that 4 total nodes are connected and properly distributed across workers.
TEST_P(ReverseConnectionClusterIntegrationTest, MultiWorkerEndToEndReverseTunnelTest) {
  DISABLE_IF_ADMIN_DISABLED; // Test requires admin interface for stats.

  // Set concurrency to 4 to create 4 workers.
  concurrency_ = 4;

  const uint32_t tunnel_listener_port =
      GetParam() == Network::Address::IpVersion::v4 ? 15000 : 15001;
  const std::string loopback_addr =
      GetParam() == Network::Address::IpVersion::v4 ? "127.0.0.1" : "::1";

  // Configure the reverse tunnel setup. Each worker will initiate its own connection.
  config_helper_.addConfigModifier([this, tunnel_listener_port, loopback_addr](
                                       envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    configureReverseTunnelSetup(bootstrap, loopback_addr, tunnel_listener_port);
  });

  // Initialize the test server with 4 workers.
  initialize();

  // Register listener ports.
  registerTestServerPorts({"tunnel_listener", "egress_listener"});

  ENVOY_LOG_MISC(info, "Waiting for all 4 workers to establish reverse tunnel connections.");

  // Each of the 4 workers should establish 1 connection, so we expect 4 total handshakes.
  test_server_->waitForCounterGe("reverse_tunnel.handshake.accepted", 4,
                                 std::chrono::milliseconds(10000));

  // Verify total node connections. Since all workers use the same node-id (test-node-id),
  // the acceptor should show 4 connections from the same logical node.
  test_server_->waitForGaugeGe("reverse_tunnel_acceptor.nodes.test-node-id", 4);
  test_server_->waitForGaugeGe("reverse_tunnel_acceptor.clusters.test-cluster-id", 4);

  // Verify no handshake errors occurred.
  EXPECT_EQ(test_server_->counter("reverse_tunnel.handshake.parse_error")->value(), 0);
  EXPECT_EQ(test_server_->counter("reverse_tunnel.handshake.rejected")->value(), 0);
  EXPECT_EQ(test_server_->counter("reverse_tunnel.handshake.validation_failed")->value(), 0);

  ENVOY_LOG_MISC(
      info, "All 4 worker tunnels established. Verifying per-worker reverse connection stats.");

  // Verify that each worker initiated exactly 1 connection (initiator side).
  ENVOY_LOG_MISC(info, "Verifying per-worker initiator connections.");
  const std::string tunnel_address = fmt::format("{}:{}", loopback_addr, tunnel_listener_port);
  // Note: IPv6 addresses use bracket notation [::1]:port in stat names
  const std::string formatted_tunnel_address =
      (GetParam() == Network::Address::IpVersion::v6)
          ? fmt::format("[{}]:{}", loopback_addr, tunnel_listener_port)
          : tunnel_address;

  for (int worker_id = 0; worker_id < 4; worker_id++) {
    // Check per-worker host stat (connected to tunnel_cluster)
    const std::string worker_host_stat =
        fmt::format("reverse_tunnel_initiator.worker_{}.host.{}.connected", worker_id,
                    formatted_tunnel_address);
    test_server_->waitForGaugeEq(worker_host_stat, 1, std::chrono::milliseconds(2000));

    // Check per-worker cluster stat
    const std::string worker_cluster_stat = fmt::format(
        "reverse_tunnel_initiator.worker_{}.cluster.tunnel_cluster.connected", worker_id);
    test_server_->waitForGaugeEq(worker_cluster_stat, 1, std::chrono::milliseconds(2000));

    ENVOY_LOG_MISC(info, "Worker {} has initiated 1 reverse connection.", worker_id);
  }

  // Verify cross-worker initiator stats (aggregated across all workers).
  const std::string cross_worker_initiator_host_stat =
      fmt::format("reverse_tunnel_initiator.host.{}.connected", formatted_tunnel_address);
  test_server_->waitForGaugeEq(cross_worker_initiator_host_stat, 4,
                               std::chrono::milliseconds(2000));
  const std::string cross_worker_initiator_cluster_stat =
      "reverse_tunnel_initiator.cluster.tunnel_cluster.connected";
  test_server_->waitForGaugeEq(cross_worker_initiator_cluster_stat, 4,
                               std::chrono::milliseconds(2000));

  // Verify that each worker accepted exactly 1 connection (acceptor side).
  ENVOY_LOG_MISC(info, "Verifying per-worker acceptor connections.");

  for (int worker_id = 0; worker_id < 4; worker_id++) {
    // Check per-worker node stat
    const std::string worker_node_stat =
        fmt::format("reverse_tunnel_acceptor.worker_{}.node.test-node-id", worker_id);
    test_server_->waitForGaugeEq(worker_node_stat, 1, std::chrono::milliseconds(2000));

    // Check per-worker cluster stat
    const std::string worker_cluster_stat =
        fmt::format("reverse_tunnel_acceptor.worker_{}.cluster.test-cluster-id", worker_id);
    test_server_->waitForGaugeEq(worker_cluster_stat, 1, std::chrono::milliseconds(2000));

    // Check per-worker aggregate metrics (total_nodes and total_clusters for each worker)
    const std::string worker_total_nodes_stat =
        fmt::format("reverse_tunnel_acceptor.worker_{}.total_nodes", worker_id);
    test_server_->waitForGaugeEq(worker_total_nodes_stat, 1, std::chrono::milliseconds(2000));

    const std::string worker_total_clusters_stat =
        fmt::format("reverse_tunnel_acceptor.worker_{}.total_clusters", worker_id);
    test_server_->waitForGaugeEq(worker_total_clusters_stat, 1, std::chrono::milliseconds(2000));

    ENVOY_LOG_MISC(info, "Worker {} has accepted 1 reverse connection.", worker_id);
  }

  // Allow time for all reverse connections to be fully registered in the cluster.
  timeSystem().advanceTimeWait(std::chrono::milliseconds(500));

  ENVOY_LOG_MISC(info, "Sending multiple requests through the multi-worker tunnel.");

  // Send multiple requests to verify load distribution across the 4 worker tunnels.
  codec_client_ = makeHttpConnection(lookupPort("egress_listener"));

  std::vector<IntegrationStreamDecoderPtr> responses;
  const int num_requests = 12; // Send 12 requests to distribute across 4 workers

  for (int i = 0; i < num_requests; i++) {
    Http::TestRequestHeaderMapImpl headers{{":method", "GET"},
                                           {":path", fmt::format("/test/path{}", i)},
                                           {":scheme", "http"},
                                           {":authority", "host"}};
    auto encoder_decoder = codec_client_->startRequest(headers);
    responses.push_back(std::move(encoder_decoder.second));
    codec_client_->sendData(encoder_decoder.first, 0, true);
  }

  ENVOY_LOG_MISC(info, "Waiting for upstream connection and streams.");

  // Wait for the upstream connection and collect all streams.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));

  std::vector<FakeStreamPtr> upstream_streams;
  for (int i = 0; i < num_requests; i++) {
    FakeStreamPtr stream;
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, stream));
    ASSERT_TRUE(stream->waitForEndStream(*dispatcher_));
    upstream_streams.push_back(std::move(stream));
  }

  // Respond to all requests.
  ENVOY_LOG_MISC(info, "Responding to {} requests.", num_requests);
  for (auto& stream : upstream_streams) {
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    stream->encodeHeaders(response_headers, true);
  }

  // Wait for all responses.
  for (auto& response : responses) {
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }

  ENVOY_LOG_MISC(info, "All {} requests completed successfully.", num_requests);

  // Verify cluster stats.
  test_server_->waitForCounterGe("cluster.reverse_connection_cluster.upstream_rq_total",
                                 num_requests);
  test_server_->waitForCounterGe("cluster.reverse_connection_cluster.upstream_rq_completed",
                                 num_requests);

  // Verify that all 4 worker tunnels are still active.
  EXPECT_EQ(test_server_->counter("reverse_tunnel.handshake.accepted")->value(), 4);

  ENVOY_LOG_MISC(info, "Multi-worker reverse tunnel test completed successfully!");

  // Cleanup connections before server shutdown.
  cleanupUpstreamAndDownstream();

  // Drain listeners via admin interface to ensure proper cleanup of reverse connection sockets
  // before workers are destroyed.
  BufferingStreamDecoderPtr drain_response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/drain_listeners", "", Http::CodecType::HTTP1, GetParam());
  EXPECT_TRUE(drain_response->complete());
  EXPECT_EQ("200", drain_response->headers().getStatusValue());

  // Wait for listeners to be fully stopped before test cleanup.
  test_server_->waitForCounterEq("listener_manager.listener_stopped", 3,
                                 std::chrono::milliseconds(5000));
}

} // namespace
} // namespace ReverseConnection
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
