#include "envoy/api/v2/discovery.pb.h"
#include "envoy/api/v2/rds.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/stats/scope.h"

#include "common/config/protobuf_link_hacks.h"
#include "common/config/resources.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/http_integration.h"
#include "test/integration/utility.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"

using testing::AssertionResult;

namespace Envoy {
namespace {

const char Config[] = R"EOF(
admin:
  access_log_path: /dev/null
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
static_resources:
  clusters:
  - name: xds_cluster
    type: STATIC
    http2_protocol_options: {}
    hosts:
      socket_address:
        address: 127.0.0.1
        port_value: 0
  - name: my_service
    type: STATIC
    http2_protocol_options: {}
    hosts:
      socket_address:
        address: 127.0.0.1
        port_value: 0
  listeners:
  - name: http
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 0
    filter_chains:
    - filters:
      - name: envoy.http_connection_manager
        typed_config:
          "@type": type.googleapis.com/envoy.config.filter.network.http_connection_manager.v2.HttpConnectionManager
          stat_prefix: config_test
          http_filters:
          - name: envoy.router
          codec_type: HTTP2
          rds:
            route_config_name: my_route
            config_source:
              api_config_source:
                api_type: GRPC
                grpc_services:
                  envoy_grpc:
                    cluster_name: xds_cluster
)EOF";

const char RdsConfig[] = R"EOF(
name: my_route
vhds:
  config_source:
    api_config_source:
      api_type: DELTA_GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: xds_cluster
)EOF";

const char RdsConfigWithVhosts[] = R"EOF(
name: my_route
virtual_hosts:
- name: vhost_rds1
  domains: ["vhost.rds.first"]
  routes:
  - match: { prefix: "/rdsone" }
    route: { cluster: my_service }
vhds:
  config_source:
    api_config_source:
      api_type: DELTA_GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: xds_cluster
)EOF";

class VhdsIntegrationTest : public HttpIntegrationTest,
                            public Grpc::GrpcClientIntegrationParamTest {
public:
  VhdsIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, ipVersion(), realTime(), Config) {
    use_lds_ = false;
  }

  void TearDown() override {
    cleanUpXdsConnection();
    test_server_.reset();
    fake_upstreams_.clear();
  }

  std::string virtualHostYaml(const std::string& name, const std::string& domain) {
    return fmt::format(R"EOF(
      name: {}
      domains: [{}]
      routes:
      - match: {{ prefix: "/" }}
        route: {{ cluster: "my_service" }}
    )EOF",
                       name, domain);
  }

  envoy::api::v2::route::VirtualHost buildVirtualHost() {
    return TestUtility::parseYaml<envoy::api::v2::route::VirtualHost>(
        virtualHostYaml("vhost_0", "host"));
  }

  std::vector<envoy::api::v2::route::VirtualHost> buildVirtualHost1() {
    return {TestUtility::parseYaml<envoy::api::v2::route::VirtualHost>(
                virtualHostYaml("vhost_1", "vhost.first")),
            TestUtility::parseYaml<envoy::api::v2::route::VirtualHost>(
                virtualHostYaml("vhost_2", "vhost.second"))};
  }

  envoy::api::v2::route::VirtualHost buildVirtualHost2() {
    return TestUtility::parseYaml<envoy::api::v2::route::VirtualHost>(
        virtualHostYaml("vhost_1", "vhost.first"));
  }

  // Overridden to insert this stuff into the initialize() at the very beginning of
  // HttpIntegrationTest::testRouterRequestAndResponseWithBody().
  void initialize() override {
    // Controls how many fake_upstreams_.emplace_back(new FakeUpstream) will happen in
    // BaseIntegrationTest::createUpstreams() (which is part of initialize()).
    // Make sure this number matches the size of the 'clusters' repeated field in the bootstrap
    // config that you use!
    setUpstreamCount(2);                                  // the CDS cluster
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2); // CDS uses gRPC uses HTTP2.

    // BaseIntegrationTest::initialize() does many things:
    // 1) It appends to fake_upstreams_ as many as you asked for via setUpstreamCount().
    // 2) It updates your bootstrap config with the ports your fake upstreams are actually listening
    //    on (since you're supposed to leave them as 0).
    // 3) It creates and starts an IntegrationTestServer - the thing that wraps the almost-actual
    //    Envoy used in the tests.
    // 4) Bringing up the server usually entails waiting to ensure that any listeners specified in
    //    the bootstrap config have come up, and registering them in a port map (see lookupPort()).
    //    However, this test needs to defer all of that to later.
    defer_listener_finalization_ = true;
    HttpIntegrationTest::initialize();

    // Now that the upstream has been created, process Envoy's request to discover it.
    // (First, we have to let Envoy establish its connection to the RDS server.)
    AssertionResult result = // xds_connection_ is filled with the new FakeHttpConnection.
        fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, xds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    xds_stream_->startGrpcStream();
    fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

    EXPECT_TRUE(compareSotwDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "",
                                            {"my_route"}, true));
    sendSotwDiscoveryResponse<envoy::api::v2::RouteConfiguration>(
        Config::TypeUrl::get().RouteConfiguration, {rdsConfig()}, "1");

    result = xds_connection_->waitForNewStream(*dispatcher_, vhds_stream_, true);
    RELEASE_ASSERT(result, result.message());
    vhds_stream_->startGrpcStream();

    EXPECT_TRUE(
        compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {}, vhds_stream_));
    sendDeltaDiscoveryResponse<envoy::api::v2::route::VirtualHost>(
        Config::TypeUrl::get().VirtualHost, {buildVirtualHost()}, {}, "1", vhds_stream_);
    EXPECT_TRUE(
        compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {}, vhds_stream_));

    // Wait for our statically specified listener to become ready, and register its port in the
    // test framework's downstream listener port map.
    test_server_->waitUntilListenersReady();
    registerTestServerPorts({"http"});
  }

  void useRdsWithVhosts() { use_rds_with_vhosts = true; }
  const envoy::api::v2::RouteConfiguration rdsConfig() const {
    return TestUtility::parseYaml<envoy::api::v2::RouteConfiguration>(
        use_rds_with_vhosts ? RdsConfigWithVhosts : RdsConfig);
  }

  FakeStreamPtr vhds_stream_;
  bool use_rds_with_vhosts{false};
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, VhdsIntegrationTest, GRPC_CLIENT_INTEGRATION_PARAMS);

// tests a scenario when:
//  - a spontaneous VHDS DiscoveryResponse adds two virtual hosts
//  - the next spontaneous VHDS DiscoveryResponse removes newly added virtual hosts
//  - Upstream makes a request to an (now) unknown domain, which fails
TEST_P(VhdsIntegrationTest, VhdsVirtualHostAddUpdateRemove) {
  // Calls our initialize(), which includes establishing a listener, route, and cluster.
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1);
  cleanupUpstreamAndDownstream();
  codec_client_->waitForDisconnect();

  // A spontaneous VHDS DiscoveryResponse adds two virtual hosts
  sendDeltaDiscoveryResponse<envoy::api::v2::route::VirtualHost>(
      Config::TypeUrl::get().VirtualHost, buildVirtualHost1(), {}, "2", vhds_stream_);
  EXPECT_TRUE(
      compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {}, vhds_stream_));

  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/one", "vhost.first");
  cleanupUpstreamAndDownstream();
  codec_client_->waitForDisconnect();
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/two", "vhost.second");
  cleanupUpstreamAndDownstream();
  codec_client_->waitForDisconnect();

  // A spontaneous VHDS DiscoveryResponse removes newly added virtual hosts
  sendDeltaDiscoveryResponse<envoy::api::v2::route::VirtualHost>(
      Config::TypeUrl::get().VirtualHost, {}, {"vhost_1", "vhost_2"}, "3", vhds_stream_);
  EXPECT_TRUE(
      compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {}, vhds_stream_));

  // an upstream request to an (now) unknown domain
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestHeaderMapImpl request_headers{{":method", "GET"},
                                          {":path", "/one"},
                                          {":scheme", "http"},
                                          {":authority", "vhost.first"},
                                          {"x-lyft-user-id", "123"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
  response->waitForHeaders();
  EXPECT_EQ("404", response->headers().Status()->value().getStringView());

  cleanupUpstreamAndDownstream();
}

// tests a scenario when:
//  - an RDS exchange contains a non-empty virtual_hosts array
//  - a spontaneous VHDS DiscoveryResponse adds two virtual hosts
//  - the next spontaneous VHDS DiscoveryResponse removes newly added virtual hosts
//  - Upstream makes a request to an (now) unknown domain, which fails
TEST_P(VhdsIntegrationTest, RdsWithVirtualHostsVhdsVirtualHostAddUpdateRemove) {
  // RDS exchange with a non-empty virtual_hosts field
  useRdsWithVhosts();

  testRouterHeaderOnlyRequestAndResponse(nullptr, 1);
  cleanupUpstreamAndDownstream();
  codec_client_->waitForDisconnect();

  // A spontaneous VHDS DiscoveryResponse adds two virtual hosts
  sendDeltaDiscoveryResponse<envoy::api::v2::route::VirtualHost>(
      Config::TypeUrl::get().VirtualHost, buildVirtualHost1(), {}, "2", vhds_stream_);
  EXPECT_TRUE(
      compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {}, vhds_stream_));

  // verify that rds-based virtual host can be resolved
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/rdsone", "vhost.rds.first");
  cleanupUpstreamAndDownstream();
  codec_client_->waitForDisconnect();
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/one", "vhost.first");
  cleanupUpstreamAndDownstream();
  codec_client_->waitForDisconnect();
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/two", "vhost.second");
  cleanupUpstreamAndDownstream();
  codec_client_->waitForDisconnect();

  // A spontaneous VHDS DiscoveryResponse removes virtual hosts added via vhds
  sendDeltaDiscoveryResponse<envoy::api::v2::route::VirtualHost>(
      Config::TypeUrl::get().VirtualHost, {}, {"vhost_1", "vhost_2"}, "3", vhds_stream_);
  EXPECT_TRUE(
      compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {}, vhds_stream_));

  // verify rds-based virtual host is still present
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/rdsone", "vhost.rds.first");
  cleanupUpstreamAndDownstream();
  codec_client_->waitForDisconnect();

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestHeaderMapImpl request_headers{{":method", "GET"},
                                          {":path", "/one"},
                                          {":scheme", "http"},
                                          {":authority", "vhost.first"},
                                          {"x-lyft-user-id", "123"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
  response->waitForHeaders();
  EXPECT_EQ("404", response->headers().Status()->value().getStringView());

  cleanupUpstreamAndDownstream();
}

} // namespace
} // namespace Envoy
