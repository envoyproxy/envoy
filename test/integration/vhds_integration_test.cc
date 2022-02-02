#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/stats/scope.h"

#include "source/common/config/protobuf_link_hacks.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/config/v2_link_hacks.h"
#include "test/integration/http_integration.h"
#include "test/integration/utility.h"
#include "test/integration/vhds.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/resources.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"

using testing::AssertionResult;

namespace Envoy {
namespace {

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

class VhdsInitializationTest : public HttpIntegrationTest,
                               public Grpc::UnifiedOrLegacyMuxIntegrationParamTest {
public:
  VhdsInitializationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion(), config()) {
    use_lds_ = false;
    if (isUnified()) {
      config_helper_.addRuntimeOverride("envoy.reloadable_features.unified_mux", "true");
    }
  }

  void TearDown() override { cleanUpXdsConnection(); }

  // Overridden to insert this stuff into the initialize() at the very beginning of
  // HttpIntegrationTest::testRouterRequestAndResponseWithBody().
  void initialize() override {
    // Controls how many addFakeUpstream() will happen in
    // BaseIntegrationTest::createUpstreams() (which is part of initialize()).
    // Make sure this number matches the size of the 'clusters' repeated field in the bootstrap
    // config that you use!
    setUpstreamCount(2);                         // the CDS cluster
    setUpstreamProtocol(Http::CodecType::HTTP2); // CDS uses gRPC uses HTTP2.

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
                         UNIFIED_LEGACY_GRPC_CLIENT_INTEGRATION_PARAMS);

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

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, VhdsIntegrationTest,
                         UNIFIED_LEGACY_GRPC_CLIENT_INTEGRATION_PARAMS);

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
TEST_P(VhdsIntegrationTest, DISABLED_VhdsVirtualHostAddUpdateRemove) {
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
TEST_P(VhdsIntegrationTest, DISABLED_RdsWithVirtualHostsVhdsVirtualHostAddUpdateRemove) {
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
/*
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

  envoy::service::discovery::v3::DeltaDiscoveryResponse vhds_update =
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
//  - Upstream makes a request to an unknown domain
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
//  - Upstream makes a request to vhost.third that cannot be resolved by the management server
//  - Management server sends a spontaneous update for vhost.first and an empty response for
//  vhost.third
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

  envoy::service::discovery::v3::DeltaDiscoveryResponse vhds_update =
      createDeltaDiscoveryResponseWithResourceNameUsedAsAlias();
  vhds_stream_->sendGrpcMessage(vhds_update);

  codec_client_->sendReset(encoder);
  ASSERT_TRUE(response->waitForReset());
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

// Verifies that invalid VHDS updates get rejected and do not affect subsequent updates
// see https://github.com/envoyproxy/envoy/issues/14918
TEST_P(VhdsIntegrationTest, AttemptAddingDuplicateDomainNames) {
  // RDS exchange with a non-empty virtual_hosts field
  useRdsWithVhosts();

  testRouterHeaderOnlyRequestAndResponse(nullptr, 1);
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // A spontaneous VHDS DiscoveryResponse with duplicate domains that results in an error in the
  // ack.
  sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
      Config::TypeUrl::get().VirtualHost,
      {TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(
           virtualHostYaml("my_route/vhost_1", "vhost.duplicate")),
       TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(
           virtualHostYaml("my_route/vhost_2", "vhost.duplicate"))},
      {}, "2", vhds_stream_);
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {}, vhds_stream_,
                                           13, "Only unique values for domains are permitted"));

  // Another update, this time valid, should result in no errors
  sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
      Config::TypeUrl::get().VirtualHost,
      {TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(
          virtualHostYaml("my_route/vhost_3", "vhost.third"))},
      {}, "2", vhds_stream_);
  EXPECT_TRUE(
      compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {}, vhds_stream_));

  cleanupUpstreamAndDownstream();
}
*/
} // namespace
} // namespace Envoy
