#pragma once

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
#include "test/test_common/network_utility.h"
#include "test/test_common/resources.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"

namespace Envoy {

const std::string& config() {
  CONSTRUCT_ON_FIRST_USE(std::string, fmt::format(R"EOF(
admin:
  access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: "{}"
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
static_resources:
  clusters:
  - name: xds_cluster
    type: STATIC
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        explicit_http_config:
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
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        explicit_http_config:
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
          "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
          stat_prefix: config_test
          http_filters:
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

constexpr absl::string_view VhostTemplate = R"EOF(
name: {}
domains: [{}]
routes:
- match: {{ prefix: "/" }}
  route: {{ cluster: "my_service" }}
)EOF";

const std::string RouteConfigName = "my_route";

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
                            public Grpc::UnifiedOrLegacyMuxIntegrationParamTest {
public:
  VhdsIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion(), config()) {
    use_lds_ = false;
    config_helper_.addRuntimeOverride("envoy.reloadable_features.unified_mux",
                                      isUnified() ? "true" : "false");
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
        virtualHostYaml("my_route/vhost_0", "sni.lyft.com"));
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
        Config::TypeUrl::get().RouteConfiguration, {rdsConfig()}, "1");

    result = xds_connection_->waitForNewStream(*dispatcher_, vhds_stream_);
    RELEASE_ASSERT(result, result.message());
    vhds_stream_->startGrpcStream();

    EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {},
                                             vhds_stream_.get()));
    sendDeltaDiscoveryResponse<envoy::config::route::v3::VirtualHost>(
        Config::TypeUrl::get().VirtualHost, {buildVirtualHost()}, {}, "1", vhds_stream_.get());
    EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().VirtualHost, {}, {},
                                             vhds_stream_.get()));

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
    envoy::service::discovery::v3::DeltaDiscoveryResponse response;
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
        Config::TypeUrl::get().VirtualHost, added_or_updated, removed, version, aliases, {});
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
  envoy::service::discovery::v3::DeltaDiscoveryResponse
  createDeltaDiscoveryResponseWithResourceNameUsedAsAlias() {
    envoy::service::discovery::v3::DeltaDiscoveryResponse ret;
    ret.set_system_version_info("system_version_info_this_is_a_test");
    ret.set_type_url(Config::TypeUrl::get().VirtualHost);

    auto* resource = ret.add_resources();
    resource->set_name("my_route/vhost_1");
    resource->set_version("4");
    resource->mutable_resource()->PackFrom(
        TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(
            virtualHostYaml("my_route/vhost_1", "vhost_1, vhost.first")));
    resource->add_aliases("my_route/vhost.first");
    ret.set_nonce("test-nonce-0");

    return ret;
  }

  FakeStreamPtr vhds_stream_;
  bool use_rds_with_vhosts{false};
};

} // namespace Envoy
