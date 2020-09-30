#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/stats/scope.h"

#include "common/config/protobuf_link_hacks.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/http_integration.h"
#include "test/integration/utility.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/resources.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"

using testing::AssertionResult;

namespace Envoy {
namespace {

const std::string& config() {
  CONSTRUCT_ON_FIRST_USE(std::string, fmt::format(R"EOF(
admin:
  access_log_path: {}
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
static_resources:
  clusters:
  - name: xds_cluster
    type: STATIC
    http2_protocol_options: {{}}
    load_assignment:
      cluster_name: xds_cluster
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 0
  - name: my_service
    type: STATIC
    http2_protocol_options: {{}}
    load_assignment:
      cluster_name: my_service
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
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
      - name: http
        typed_config:
          "@type": type.googleapis.com/envoy.config.filter.network.http_connection_manager.v2.HttpConnectionManager
          stat_prefix: config_test
          http_filters:
          - name: envoy.filters.http.on_demand
          - name: envoy.filters.http.router
          codec_type: HTTP2
          rds:
            route_config_name: my_route
            config_source:
              api_config_source:
                api_type: GRPC
                grpc_services:
                  envoy_grpc:
                    cluster_name: xds_cluster
)EOF",
                                                  Platform::null_device_path));
}

// TODO (dmitri-d) move config yaml into ConfigHelper
const char RdsWithoutVhdsConfig[] = R"EOF(
name: my_route
virtual_hosts:
- name: vhost_rds1
  domains: ["vhost.rds.first"]
  routes:
  - match: { prefix: "/rdsone" }
    route: { cluster: my_service }
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

const std::string RouteConfigName = "my_route";

const char VhostTemplate[] = R"EOF(
name: {}
domains: [{}]
routes:
- match: {{ prefix: "/" }}
  route: {{ cluster: "my_service" }}
)EOF";

class VhdsInitializationTest : public HttpIntegrationTest,
                               public Grpc::GrpcClientIntegrationParamTest {
public:
  VhdsInitializationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, ipVersion(), realTime(), config()) {
    use_lds_ = false;
  }

  void TearDown() override { cleanUpXdsConnection(); }

  // Overridden to insert this stuff into the initialize() at the very beginning of
  // HttpIntegrationTest::testRouterRequestAndResponseWithBody().
  void initialize() override {
    // Controls how many addFakeUpstream() will happen in
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

    EXPECT_TRUE(compareSotwDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "",
                                            {"my_route"}, true));
    sendSotwDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
        Config::TypeUrl::get().RouteConfiguration,
        {TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(
            RdsWithoutVhdsConfig)},
        "1");

    // Wait for our statically specified listener to become ready, and register its port in the
    // test framework's downstream listener port map.
    test_server_->waitUntilListenersReady();
    registerTestServerPorts({"http"});
  }

  FakeStreamPtr vhds_stream_;
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, VhdsInitializationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

// tests a scenario when:
//  - RouteConfiguration without VHDS is received
//  - RouteConfiguration update with VHDS configuration in it is received
//  - Upstream makes a request to a VirtualHost in the VHDS update
TEST_P(VhdsInitializationTest, InitializeVhdsAfterRdsHasBeenInitialized) {
  // Calls our initialize(), which includes establishing a listener, route, and cluster.
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/rdsone", "vhost.rds.first");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Update RouteConfig, this time include VHDS config
  sendSotwDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration,
      {TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(RdsConfigWithVhosts)},
      "2");

  auto result = xds_connection_->waitForNewStream(*dispatcher_, vhds_stream_);
  RELEASE_ASSERT(result, result.message());
  vhds_stream_->startGrpcStream();

  EXPECT_TRUE(
      compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {}, vhds_stream_));
  sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
      Config::TypeUrl::get().VirtualHost,
      {TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(
          fmt::format(VhostTemplate, "my_route/vhost_0", "vhost.first"))},
      {}, "1", vhds_stream_);
  EXPECT_TRUE(
      compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {}, vhds_stream_));

  // Confirm vhost.first that was configured via VHDS is reachable
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/", "vhost.first");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());
}

class VhdsIntegrationTest : public HttpIntegrationTest,
                            public Grpc::GrpcClientIntegrationParamTest {
public:
  VhdsIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, ipVersion(), realTime(), config()) {
    use_lds_ = false;
  }

  void TearDown() override { cleanUpXdsConnection(); }

  std::string virtualHostYaml(const std::string& name, const std::string& domain) {
    return fmt::format(VhostTemplate, name, domain);
  }

  std::string vhdsRequestResourceName(const std::string& host_header) {
    return RouteConfigName + "/" + host_header;
  }

  envoy::config::route::v3::VirtualHost buildVirtualHost() {
    return TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(
        virtualHostYaml("my_route/vhost_0", "host"));
  }

  std::vector<envoy::config::route::v3::VirtualHost> buildVirtualHost1() {
    return {TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(
                virtualHostYaml("my_route/vhost_1", "vhost.first")),
            TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(
                virtualHostYaml("my_route/vhost_2", "vhost.second"))};
  }

  envoy::config::route::v3::VirtualHost buildVirtualHost2() {
    return TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(
        virtualHostYaml("my_route/vhost_1", "vhost.first"));
  }

  // Overridden to insert this stuff into the initialize() at the very beginning of
  // HttpIntegrationTest::testRouterRequestAndResponseWithBody().
  void initialize() override {
    // Controls how many addFakeUpstream() will happen in
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

    EXPECT_TRUE(compareSotwDiscoveryRequest(Config::TypeUrl::get().RouteConfiguration, "",
                                            {"my_route"}, true));
    sendSotwDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
        Config::TypeUrl::get().RouteConfiguration, {rdsConfig()}, "1");

    result = xds_connection_->waitForNewStream(*dispatcher_, vhds_stream_);
    RELEASE_ASSERT(result, result.message());
    vhds_stream_->startGrpcStream();

    EXPECT_TRUE(
        compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {}, vhds_stream_));
    sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
        Config::TypeUrl::get().VirtualHost, {buildVirtualHost()}, {}, "1", vhds_stream_);
    EXPECT_TRUE(
        compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {}, vhds_stream_));

    // Wait for our statically specified listener to become ready, and register its port in the
    // test framework's downstream listener port map.
    test_server_->waitUntilListenersReady();
    registerTestServerPorts({"http"});
  }

  void useRdsWithVhosts() { use_rds_with_vhosts = true; }
  const envoy::config::route::v3::RouteConfiguration rdsConfig() const {
    return TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(
        use_rds_with_vhosts ? RdsConfigWithVhosts : RdsConfig);
  }

  void notifyAboutAliasResolutionFailure(const std::string& version, FakeStreamPtr& stream,
                                         const std::vector<std::string>& aliases = {}) {
    envoy::api::v2::DeltaDiscoveryResponse response;
    response.set_system_version_info("system_version_info_this_is_a_test");
    response.set_type_url(Config::TypeUrl::get().VirtualHost);
    auto* resource = response.add_resources();
    resource->set_name("my_route/cannot-resolve-alias");
    resource->set_version(version);
    for (const auto& alias : aliases) {
      resource->add_aliases(alias);
    }
    response.set_nonce("noncense");
    stream->sendGrpcMessage(response);
  }

  void sendDeltaDiscoveryResponseWithUnresolvedAliases(
      const std::vector<envoy::config::route::v3::VirtualHost>& added_or_updated,
      const std::vector<std::string>& removed, const std::string& version, FakeStreamPtr& stream,
      const std::vector<std::string>& aliases, const std::vector<std::string>& unresolved_aliases) {
    auto response = createDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
        Config::TypeUrl::get().VirtualHost, added_or_updated, removed, version, aliases);
    for (const auto& unresolved_alias : unresolved_aliases) {
      auto* resource = response.add_resources();
      resource->set_name(unresolved_alias);
      resource->set_version(version);
      resource->add_aliases(unresolved_alias);
    }
    stream->sendGrpcMessage(response);
  }

  // used in VhdsOnDemandUpdateWithResourceNameAsAlias test
  // to create a DeltaDiscoveryResponse with a resource name matching the value used to create an
  // on-demand request
  envoy::api::v2::DeltaDiscoveryResponse createDeltaDiscoveryResponseWithResourceNameUsedAsAlias() {
    API_NO_BOOST(envoy::api::v2::DeltaDiscoveryResponse) ret;
    ret.set_system_version_info("system_version_info_this_is_a_test");
    ret.set_type_url(Config::TypeUrl::get().VirtualHost);

    auto* resource = ret.add_resources();
    resource->set_name("my_route/vhost_1");
    resource->set_version("4");
    resource->mutable_resource()->PackFrom(
        API_DOWNGRADE(TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(
            virtualHostYaml("my_route/vhost_1", "vhost_1, vhost.first"))));
    resource->add_aliases("my_route/vhost.first");
    ret.set_nonce("test-nonce-0");

    return ret;
  }

  FakeStreamPtr vhds_stream_;
  bool use_rds_with_vhosts{false};
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, VhdsIntegrationTest, GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(VhdsIntegrationTest, RdsUpdateWithoutVHDSChangesDoesNotRestartVHDS) {
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/", "host");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Update RouteConfig, but don't change VHDS config
  sendSotwDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration,
      {TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(RdsConfigWithVhosts)},
      "2");

  // Confirm vhost_0 that was originally configured via VHDS is reachable
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/", "host");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());
}

// tests a scenario when:
//  - a spontaneous VHDS DiscoveryResponse adds two virtual hosts
//  - the next spontaneous VHDS DiscoveryResponse removes newly added virtual hosts
//  - Upstream makes a request to an (now) unknown domain
//  - A VHDS DiscoveryResponse received containing update for the domain
//  - Upstream receives a 200 response
TEST_P(VhdsIntegrationTest, VhdsVirtualHostAddUpdateRemove) {
  // Calls our initialize(), which includes establishing a listener, route, and cluster.
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1);
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // A spontaneous VHDS DiscoveryResponse adds two virtual hosts
  sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
      Config::TypeUrl::get().VirtualHost, buildVirtualHost1(), {}, "2", vhds_stream_);
  EXPECT_TRUE(
      compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {}, vhds_stream_));

  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/one", "vhost.first");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/two", "vhost.second");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // A spontaneous VHDS DiscoveryResponse removes newly added virtual hosts
  sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
      Config::TypeUrl::get().VirtualHost, {}, {"my_route/vhost_1", "my_route/vhost_2"}, "3",
      vhds_stream_);
  EXPECT_TRUE(
      compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {}, vhds_stream_));

  // an upstream request to an (now) unknown domain
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.first"},
                                                 {"x-lyft-user-id", "123"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost,
                                           {vhdsRequestResourceName("vhost.first")}, {},
                                           vhds_stream_));
  sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
      Config::TypeUrl::get().VirtualHost, {buildVirtualHost2()}, {}, "4", vhds_stream_,
      {"my_route/vhost.first"});

  waitForNextUpstreamRequest(1);
  // Send response headers, and end_stream if there is no response body.
  upstream_request_->encodeHeaders(default_response_headers_, true);

  response->waitForHeaders();
  EXPECT_EQ("200", response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
}

// tests a scenario when:
//  - an RDS exchange contains a non-empty virtual_hosts array
//  - a spontaneous VHDS DiscoveryResponse adds two virtual hosts
//  - the next spontaneous VHDS DiscoveryResponse removes newly added virtual hosts
//  - Upstream makes a request to an (now) unknown domain
//  - A VHDS DiscoveryResponse received containing update for the domain
//  - Upstream receives a 200 response
TEST_P(VhdsIntegrationTest, RdsWithVirtualHostsVhdsVirtualHostAddUpdateRemove) {
  // RDS exchange with a non-empty virtual_hosts field
  useRdsWithVhosts();

  testRouterHeaderOnlyRequestAndResponse(nullptr, 1);
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // A spontaneous VHDS DiscoveryResponse adds two virtual hosts
  sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
      Config::TypeUrl::get().VirtualHost, buildVirtualHost1(), {}, "2", vhds_stream_);
  EXPECT_TRUE(
      compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {}, vhds_stream_));

  // verify that rds-based virtual host can be resolved
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/rdsone", "vhost.rds.first");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/one", "vhost.first");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/two", "vhost.second");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // A spontaneous VHDS DiscoveryResponse removes virtual hosts added via vhds
  sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
      Config::TypeUrl::get().VirtualHost, {}, {"my_route/vhost_1", "my_route/vhost_2"}, "3",
      vhds_stream_);
  EXPECT_TRUE(
      compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {}, vhds_stream_));

  // verify rds-based virtual host is still present
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/rdsone", "vhost.rds.first");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.first"},
                                                 {"x-lyft-user-id", "123"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost,
                                           {vhdsRequestResourceName("vhost.first")}, {},
                                           vhds_stream_));
  sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
      Config::TypeUrl::get().VirtualHost, {buildVirtualHost2()}, {}, "4", vhds_stream_,
      {"my_route/vhost.first"});

  waitForNextUpstreamRequest(1);
  // Send response headers, and end_stream if there is no response body.
  upstream_request_->encodeHeaders(default_response_headers_, true);

  response->waitForHeaders();
  EXPECT_EQ("200", response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
}

// tests a scenario where:
//  a Resource received in a DeltaDiscoveryResponse has name that matches the value used in the
//  on-demand request
TEST_P(VhdsIntegrationTest, VhdsOnDemandUpdateWithResourceNameAsAlias) {
  // RDS exchange with a non-empty virtual_hosts field
  useRdsWithVhosts();

  testRouterHeaderOnlyRequestAndResponse(nullptr, 1);
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // verify that rds-based virtual host can be resolved
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/rdsone", "vhost.rds.first");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Attempt to make a request to an unknown host
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost_1"},
                                                 {"x-lyft-user-id", "123"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost,
                                           {vhdsRequestResourceName("vhost_1")}, {}, vhds_stream_));

  envoy::api::v2::DeltaDiscoveryResponse vhds_update =
      createDeltaDiscoveryResponseWithResourceNameUsedAsAlias();
  vhds_stream_->sendGrpcMessage(vhds_update);

  waitForNextUpstreamRequest(1);
  // Send response headers, and end_stream if there is no response body.
  upstream_request_->encodeHeaders(default_response_headers_, true);

  response->waitForHeaders();
  EXPECT_EQ("200", response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
}

// tests a scenario when:
//  - an RDS exchange contains a non-empty virtual_hosts array
//  - a spontaneous VHDS DiscoveryResponse adds two virtual hosts
//  - the next spontaneous VHDS DiscoveryResponse removes newly added virtual hosts
//  - Upstream makes a request to an (now) unknown domain
//  - A VHDS DiscoveryResponse received but contains no update for the domain (the management server
//  couldn't resolve it)
//  - Upstream receives a 404 response
TEST_P(VhdsIntegrationTest, VhdsOnDemandUpdateFailToResolveTheAlias) {
  // RDS exchange with a non-empty virtual_hosts field
  useRdsWithVhosts();

  testRouterHeaderOnlyRequestAndResponse(nullptr, 1);
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // verify that rds-based virtual host can be resolved
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/rdsone", "vhost.rds.first");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Attempt to make a request to an unknown host
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.third"},
                                                 {"x-lyft-user-id", "123"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost,
                                           {vhdsRequestResourceName("vhost.third")}, {},
                                           vhds_stream_));
  // Send an empty response back (the management server isn't aware of vhost.third)
  notifyAboutAliasResolutionFailure("4", vhds_stream_, {"my_route/vhost.third"});

  response->waitForHeaders();
  EXPECT_EQ("404", response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
}

// tests a scenario when:
//  - an RDS exchange contains a non-empty virtual_hosts array
//  - a spontaneous VHDS DiscoveryResponse adds two virtual hosts
//  - the next spontaneous VHDS DiscoveryResponse removes newly added virtual hosts
//  - Upstream makes a request to an (now) unknown domain
//  - A VHDS DiscoveryResponse received that contains update for vhost.first host, but vhost.third
//  couldn't be resolved
//  - Upstream receives a 404 response
TEST_P(VhdsIntegrationTest, VhdsOnDemandUpdateFailToResolveOneAliasOutOfSeveral) {
  // RDS exchange with a non-empty virtual_hosts field
  useRdsWithVhosts();

  testRouterHeaderOnlyRequestAndResponse(nullptr, 1);
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // verify that rds-based virtual host can be resolved
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/rdsone", "vhost.rds.first");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Attempt to make a request to an unknown host
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.third"},
                                                 {"x-lyft-user-id", "123"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost,
                                           {vhdsRequestResourceName("vhost.third")}, {},
                                           vhds_stream_));
  // Send an empty response back (the management server isn't aware of vhost.third)
  sendDeltaDiscoveryResponseWithUnresolvedAliases({buildVirtualHost2()}, {}, "4", vhds_stream_,
                                                  {"my_route/vhost.first"},
                                                  {"my_route/vhost.third"});

  response->waitForHeaders();
  EXPECT_EQ("404", response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
}

// Verify that an vhds update succeeds even when the client closes its connection
TEST_P(VhdsIntegrationTest, VhdsOnDemandUpdateHttpConnectionCloses) {
  // RDS exchange with a non-empty virtual_hosts field
  useRdsWithVhosts();

  testRouterHeaderOnlyRequestAndResponse(nullptr, 1);
  cleanupUpstreamAndDownstream();
  EXPECT_TRUE(codec_client_->waitForDisconnect());

  // Attempt to make a request to an unknown host
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost_1"},
                                                 {"x-lyft-user-id", "123"}};
  auto encoder_decoder = codec_client_->startRequest(request_headers);
  Http::RequestEncoder& encoder = encoder_decoder.first;
  IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost,
                                           {vhdsRequestResourceName("vhost_1")}, {}, vhds_stream_));

  envoy::api::v2::DeltaDiscoveryResponse vhds_update =
      createDeltaDiscoveryResponseWithResourceNameUsedAsAlias();
  vhds_stream_->sendGrpcMessage(vhds_update);

  codec_client_->sendReset(encoder);
  response->waitForReset();
  EXPECT_TRUE(codec_client_->connected());

  cleanupUpstreamAndDownstream();
}

const char VhostTemplateAfterUpdate[] = R"EOF(
name: {}
domains: [{}]
routes:
- match: {{ prefix: "/after_update" }}
  route: {{ cluster: "my_service" }}
)EOF";

// Verifies that after multiple vhds updates, virtual hosts from earlier updates still can receive
// updates See https://github.com/envoyproxy/envoy/issues/12158 for more details
TEST_P(VhdsIntegrationTest, MultipleUpdates) {
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1);
  cleanupUpstreamAndDownstream();
  EXPECT_TRUE(codec_client_->waitForDisconnect());

  {
    // make first vhds request (for vhost.first)
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/"},
                                                   {":scheme", "http"},
                                                   {":authority", "vhost.first"},
                                                   {"x-lyft-user-id", "123"}};
    IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
    EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost,
                                             {vhdsRequestResourceName("vhost.first")}, {},
                                             vhds_stream_));
    sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
        Config::TypeUrl::get().VirtualHost, {buildVirtualHost2()}, {}, "4", vhds_stream_,
        {"my_route/vhost.first"});
    EXPECT_TRUE(
        compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {}, vhds_stream_));

    waitForNextUpstreamRequest(1);
    // Send response headers, and end_stream if there is no response body.
    upstream_request_->encodeHeaders(default_response_headers_, true);

    response->waitForHeaders();
    EXPECT_EQ("200", response->headers().getStatusValue());

    cleanupUpstreamAndDownstream();
    EXPECT_TRUE(codec_client_->waitForDisconnect());
  }
  {
    // make second vhds request (for vhost.second)
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/"},
                                                   {":scheme", "http"},
                                                   {":authority", "vhost.second"},
                                                   {"x-lyft-user-id", "123"}};
    IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
    EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost,
                                             {vhdsRequestResourceName("vhost.second")}, {},
                                             vhds_stream_));
    sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
        Config::TypeUrl::get().VirtualHost,
        {TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(
            virtualHostYaml("my_route/vhost_2", "vhost.second"))},
        {}, "4", vhds_stream_, {"my_route/vhost.second"});
    EXPECT_TRUE(
        compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {}, vhds_stream_));

    waitForNextUpstreamRequest(1);
    // Send response headers, and end_stream if there is no response body.
    upstream_request_->encodeHeaders(default_response_headers_, true);

    response->waitForHeaders();
    EXPECT_EQ("200", response->headers().getStatusValue());

    cleanupUpstreamAndDownstream();
    EXPECT_TRUE(codec_client_->waitForDisconnect());
  }
  {
    // Attempt to push updates for both vhost.first and vhost.second
    sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
        Config::TypeUrl::get().VirtualHost,
        {TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(
             fmt::format(VhostTemplateAfterUpdate, "my_route/vhost_1", "vhost.first")),
         TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(
             fmt::format(VhostTemplateAfterUpdate, "my_route/vhost_2", "vhost.second"))},
        {}, "5", vhds_stream_);
    EXPECT_TRUE(
        compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {}, vhds_stream_));

    // verify that both virtual hosts have been updated
    testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/after_update", "vhost.first");
    cleanupUpstreamAndDownstream();
    ASSERT_TRUE(codec_client_->waitForDisconnect());

    testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/after_update", "vhost.second");
    cleanupUpstreamAndDownstream();
    ASSERT_TRUE(codec_client_->waitForDisconnect());
  }
}

} // namespace
} // namespace Envoy
