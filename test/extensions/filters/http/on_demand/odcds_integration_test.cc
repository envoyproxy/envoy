#include <string>

#include "envoy/common/platform.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/filters/http/on_demand/v3/on_demand.pb.h"
#include "envoy/extensions/filters/http/on_demand/v3/on_demand.pb.validate.h"

#include "source/common/common/fmt.h"
#include "source/common/common/macros.h"
#include "source/extensions/filters/http/well_known_names.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/ads_integration.h"
#include "test/integration/fake_upstream.h"
#include "test/integration/http_integration.h"
#include "test/test_common/resources.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class OdCdsIntegrationHelper {
public:
  using OnDemandConfig = envoy::extensions::filters::http::on_demand::v3::OnDemand;
  using PerRouteConfig = envoy::extensions::filters::http::on_demand::v3::PerRouteConfig;

  // Get the base config, without any listeners. Similar to ConfigHelper::baseConfig(), but there's
  // no lds_config nor any listeners configured, and no TLS certificate stuff.
  static std::string baseBootstrapConfig() {
    return fmt::format(R"EOF(
admin:
  access_log:
    name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: "{}"
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
static_resources:
  clusters:
    name: cluster_0
    load_assignment:
      cluster_name: cluster_0
      endpoints:
        lb_endpoints:
          endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 0
)EOF",
                       Platform::null_device_path);
  }

  // Get the listener configuration. Listener named "listener_0" comes with the on_demand HTTP
  // filter enabled, but with an empty config (so ODCDS is disabled), comes with "integration"
  // vhost, which has "odcds_route" with cluster_header action looking for cluster name in the
  // "Pick-This-Cluster" HTTP header.
  static std::string listenerConfig(absl::string_view address) {
    // Can't use ConfigHelper::buildBaseListener, because it returns a proto object, not a config as
    // a string.
    return fmt::format(R"EOF(
    name: listener_0
    address:
      socket_address:
        address: {}
        port_value: 0
    filter_chains:
      filters:
        name: http
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
          stat_prefix: config_test
          delayed_close_timeout:
            nanos: 10000000
          http_filters:
          - name: envoy.filters.http.on_demand
          - name: envoy.filters.http.router
          codec_type: HTTP2
          access_log:
            name: accesslog
            filter:
              not_health_check_filter:  {{}}
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
              path: {}
          route_config:
            virtual_hosts:
              name: integration
              routes:
                name: odcds_route
                route:
                  cluster_header: "Pick-This-Cluster"
                match:
                  prefix: "/"
              domains: "*"
            name: route_config_0
)EOF",
                       address, Platform::null_device_path);
  }

  // Get the config, with a static listener. Similar to ConfigHelper::baseConfig(), but there's no
  // lds_config and no TLS certificate stuff. Listener is configured with listenerConfig.
  static std::string bootstrapConfig() {
    return absl::StrCat(baseBootstrapConfig(), "  listeners:", listenerConfig("127.0.0.1"));
  }

  static envoy::config::core::v3::ConfigSource
  createOdCdsConfigSource(absl::string_view cluster_name) {
    envoy::config::core::v3::ConfigSource source;
    TestUtility::loadFromYaml(fmt::format(R"EOF(
      resource_api_version: V3
      api_config_source:
        api_type: DELTA_GRPC
        transport_api_version: V3
        grpc_services:
          envoy_grpc:
            cluster_name: {}
)EOF",
                                          cluster_name),
                              source);
    return source;
  }

  static envoy::config::core::v3::ConfigSource createAdsOdCdsConfigSource() {
    envoy::config::core::v3::ConfigSource source;
    TestUtility::loadFromYaml(R"EOF(
      resource_api_version: V3
      ads: {}
)EOF",
                              source);
    return source;
  }

  template <typename OnDemandConfigType>
  static OnDemandConfigType createConfig(envoy::config::core::v3::ConfigSource config_source,
                                         int timeout_millis) {
    OnDemandConfigType on_demand;
    *on_demand.mutable_odcds_config() = std::move(config_source);
    *on_demand.mutable_odcds_timeout() = ProtobufUtil::TimeUtil::MillisecondsToDuration(timeout_millis);
    return on_demand;
  }

  static OnDemandConfig createOnDemandConfig(envoy::config::core::v3::ConfigSource config_source,
                                             int timeout_millis) {
    return createConfig<OnDemandConfig>(std::move(config_source), timeout_millis);
  }

  static PerRouteConfig createPerRouteConfig(envoy::config::core::v3::ConfigSource config_source,
                                             int timeout_millis) {
    return createConfig<PerRouteConfig>(std::move(config_source), timeout_millis);
  }

  static OptRef<Protobuf::Map<std::string, ProtobufWkt::Any>>
  findPerRouteConfigMap(ConfigHelper::HttpConnectionManager& hcm, absl::string_view vhost_name,
                        absl::string_view route_name) {
    auto* route_config = hcm.mutable_route_config();
    auto* vhosts = route_config->mutable_virtual_hosts();
    for (int i = 0; i < route_config->virtual_hosts_size(); ++i) {
      auto* vhost = vhosts->Mutable(i);
      if (vhost->name() == vhost_name) {
        if (route_name.empty()) {
          return *vhost->mutable_typed_per_filter_config();
        } else {
          auto* routes = vhost->mutable_routes();
          for (int j = 0; j < vhost->routes_size(); ++j) {
            auto* route = routes->Mutable(j);
            if (route->name() == route_name) {
              return *route->mutable_typed_per_filter_config();
            }
          }
        }
      }
    }
    return absl::nullopt;
  }

  static void clearOnDemandConfig(ConfigHelper::HttpConnectionManager& hcm) {
    auto* filters = hcm.mutable_http_filters();
    for (int i = 0; i < hcm.http_filters_size(); ++i) {
      auto* filter = filters->Mutable(i);
      if (filter->name() == Extensions::HttpFilters::HttpFilterNames::get().OnDemand) {
        filter->clear_typed_config();
        break;
      }
    }
  }

  static void clearPerRouteConfig(ConfigHelper::HttpConnectionManager& hcm, std::string vhost_name,
                                  std::string route_name) {
    auto maybe_map = findPerRouteConfigMap(hcm, vhost_name, route_name);
    if (maybe_map.has_value()) {
      maybe_map->erase(Extensions::HttpFilters::HttpFilterNames::get().OnDemand);
    }
  }

  static void addOnDemandConfig(ConfigHelper::HttpConnectionManager& hcm, OnDemandConfig config) {
    auto* filters = hcm.mutable_http_filters();
    for (int i = 0; i < hcm.http_filters_size(); ++i) {
      auto* filter = filters->Mutable(i);
      if (filter->name() == Extensions::HttpFilters::HttpFilterNames::get().OnDemand) {
        filter->clear_typed_config();
        filter->mutable_typed_config()->PackFrom(std::move(config));
        break;
      }
    }
  }

  static void addPerRouteConfig(ConfigHelper::HttpConnectionManager& hcm, PerRouteConfig config,
                                std::string vhost_name, std::string route_name) {
    auto maybe_map = findPerRouteConfigMap(hcm, vhost_name, route_name);
    if (maybe_map.has_value()) {
      maybe_map.ref()[Extensions::HttpFilters::HttpFilterNames::get().OnDemand].PackFrom(
          std::move(config));
    }
  }
};

class OdCdsListenerBuilder {
public:
  OdCdsListenerBuilder(absl::string_view address) {
    TestUtility::loadFromYaml(OdCdsIntegrationHelper::listenerConfig(address), listener_);
    auto* filter_chain = listener_.mutable_filter_chains(0);
    auto* filter = filter_chain->mutable_filters(0);
    hcm_any_ = filter->mutable_typed_config();
    hcm_ = MessageUtil::anyConvert<ConfigHelper::HttpConnectionManager>(*hcm_any_);
  }

  ConfigHelper::HttpConnectionManager& hcm() { return hcm_; }
  envoy::config::listener::v3::Listener listener() {
    hcm_any_->PackFrom(hcm_);
    return listener_;
  }

private:
  envoy::config::listener::v3::Listener listener_;
  ProtobufWkt::Any* hcm_any_;
  ConfigHelper::HttpConnectionManager hcm_;
};

class OdCdsIntegrationTestBase : public HttpIntegrationTest,
                                 public Grpc::GrpcClientIntegrationParamTest {
public:
  OdCdsIntegrationTestBase()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, ipVersion(),
                            OdCdsIntegrationHelper::bootstrapConfig()) {}

  void clearOnDemandConfig() {
    config_helper_.addConfigModifier([](ConfigHelper::HttpConnectionManager& hcm) {
      OdCdsIntegrationHelper::clearOnDemandConfig(hcm);
    });
  }

  void clearPerRouteConfig(std::string vhost_name, std::string route_name) {
    config_helper_.addConfigModifier(
        [vhost_name = std::move(vhost_name),
         route_name = std::move(route_name)](ConfigHelper::HttpConnectionManager& hcm) {
          OdCdsIntegrationHelper::clearPerRouteConfig(hcm, vhost_name, route_name);
        });
  }

  void addOnDemandConfig(OdCdsIntegrationHelper::OnDemandConfig config) {
    config_helper_.addConfigModifier(
        [config = std::move(config)](ConfigHelper::HttpConnectionManager& hcm) {
          OdCdsIntegrationHelper::addOnDemandConfig(hcm, std::move(config));
        });
  }

  void addPerRouteConfig(OdCdsIntegrationHelper::PerRouteConfig config, std::string vhost_name,
                         std::string route_name) {
    config_helper_.addConfigModifier(
        [config = std::move(config), vhost_name = std::move(vhost_name),
         route_name = std::move(route_name)](ConfigHelper::HttpConnectionManager& hcm) {
          OdCdsIntegrationHelper::addPerRouteConfig(hcm, std::move(config), vhost_name, route_name);
        });
  }

  void TearDown() override {
    if (doCleanUpXdsConnection_) {
      cleanUpXdsConnection();
    }
  }

  void initialize() override {
    // Controls how many addFakeUpstream() will happen in
    // BaseIntegrationTest::createUpstreams() (which is part of initialize()).
    // Make sure this number matches the size of the 'clusters' repeated field in the bootstrap
    // config that you use!
    setUpstreamCount(1);                                  // The xDS cluster
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2); // CDS uses gRPC uses HTTP2.

    // BaseIntegrationTest::initialize() does many things:
    // 1) It appends to fake_upstreams_ as many as you asked for via setUpstreamCount().
    // 2) It updates your bootstrap config with the ports your fake upstreams are actually listening
    //    on (since you're supposed to leave them as 0).
    // 3) It creates and starts an IntegrationTestServer - the thing that wraps the almost-actual
    //    Envoy used in the tests.
    // 4) Bringing up the server usually entails waiting to ensure that any listeners specified in
    //    the bootstrap config have come up, and registering them in a port map (see lookupPort()).
    HttpIntegrationTest::initialize();

    addFakeUpstream(FakeHttpConnection::Type::HTTP2);
    new_cluster_ = ConfigHelper::buildStaticCluster(
        "new_cluster", fake_upstreams_[1]->localAddress()->ip()->port(),
        Network::Test::getLoopbackAddressString(ipVersion()));

    test_server_->waitUntilListenersReady();
    registerTestServerPorts({"http"});
  }

  FakeStreamPtr odcds_stream_;
  envoy::config::cluster::v3::Cluster new_cluster_;
  bool doCleanUpXdsConnection_ = true;
};

using OdCdsIntegrationTest = OdCdsIntegrationTestBase;

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, OdCdsIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

// tests a scenario when:
//  - making a request to an unknown cluster
//  - odcds initiates a connection with a request for the cluster
//  - a response contains the cluster
//  - request is resumed
TEST_P(OdCdsIntegrationTest, OnDemandClusterDiscoveryWorksWithClusterHeader) {
  addPerRouteConfig(OdCdsIntegrationHelper::createPerRouteConfig(
                        OdCdsIntegrationHelper::createOdCdsConfigSource("cluster_0"), 2500),
                    "integration", {});
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.first"},
                                                 {"Pick-This-Cluster", "new_cluster"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);

  auto result = // xds_connection_ is filled with the new FakeHttpConnection.
      fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, xds_connection_);
  RELEASE_ASSERT(result, result.message());
  result = xds_connection_->waitForNewStream(*dispatcher_, odcds_stream_);
  RELEASE_ASSERT(result, result.message());
  odcds_stream_->startGrpcStream();

  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().Cluster, {"new_cluster"}, {},
                                           odcds_stream_));
  sendDeltaDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TypeUrl::get().Cluster, {new_cluster_}, {}, "1", odcds_stream_);
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().Cluster, {}, {}, odcds_stream_));

  waitForNextUpstreamRequest(1);
  // Send response headers, and end_stream if there is no response body.
  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "200", {}, {});

  cleanupUpstreamAndDownstream();
}

// tests a scenario when:
//  - making a request to an unknown cluster
//  - odcds initiates a connection with a request for the cluster
//  - no response happens, timeout is triggered
//  - request is resumed
TEST_P(OdCdsIntegrationTest, OnDemandClusterDiscoveryTimesOut) {
  addPerRouteConfig(OdCdsIntegrationHelper::createPerRouteConfig(
                        OdCdsIntegrationHelper::createOdCdsConfigSource("cluster_0"), 500),
                    "integration", {});
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.first"},
                                                 {"Pick-This-Cluster", "new_cluster"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);

  auto result = // xds_connection_ is filled with the new FakeHttpConnection.
      fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, xds_connection_);
  RELEASE_ASSERT(result, result.message());
  result = xds_connection_->waitForNewStream(*dispatcher_, odcds_stream_);
  RELEASE_ASSERT(result, result.message());
  odcds_stream_->startGrpcStream();

  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().Cluster, {"new_cluster"}, {},
                                           odcds_stream_));
  // not sending a response to trigger the timeout

  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "503", {}, {});

  cleanupUpstreamAndDownstream();
}

// tests a scenario when:
//  - making a request to an unknown cluster
//  - odcds initiates a connection with a request for the cluster
//  - a response says the there is no such cluster
//  - request is resumed
TEST_P(OdCdsIntegrationTest, OnDemandClusterDiscoveryForNonexistentCluster) {
  addPerRouteConfig(OdCdsIntegrationHelper::createPerRouteConfig(
                        OdCdsIntegrationHelper::createOdCdsConfigSource("cluster_0"), 2500),
                    "integration", {});
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.first"},
                                                 {"Pick-This-Cluster", "new_cluster"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);

  auto result = // xds_connection_ is filled with the new FakeHttpConnection.
      fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, xds_connection_);
  RELEASE_ASSERT(result, result.message());
  result = xds_connection_->waitForNewStream(*dispatcher_, odcds_stream_);
  RELEASE_ASSERT(result, result.message());
  odcds_stream_->startGrpcStream();

  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().Cluster, {"new_cluster"}, {},
                                           odcds_stream_));
  sendDeltaDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TypeUrl::get().Cluster, {}, {"new_cluster"}, "1", odcds_stream_);
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().Cluster, {}, {}, odcds_stream_));

  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "503", {}, {});

  cleanupUpstreamAndDownstream();
}

// tests a scenario when:
//  - ODCDS is enabled at a virtual host level
//  - ODCDS is disabled at a route level
//  - making a request to an unknown cluster
//  - request fails
TEST_P(OdCdsIntegrationTest, DisablingOdCdsAtRouteLevelWorks) {
  doCleanUpXdsConnection_ = false;
  addPerRouteConfig(OdCdsIntegrationHelper::createPerRouteConfig(
                        OdCdsIntegrationHelper::createOdCdsConfigSource("cluster_0"), 2500),
                    "integration", {});
  addPerRouteConfig(OdCdsIntegrationHelper::PerRouteConfig(), "integration", "odcds_route");
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.first"},
                                                 {"Pick-This-Cluster", "new_cluster"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);

  EXPECT_FALSE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, xds_connection_,
                                                         std::chrono::milliseconds(1000)));

  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "503", {}, {});

  cleanupUpstreamAndDownstream();
}

// tests a scenario when:
//  - ODCDS is enabled in http connection manager
//  - ODCDS is disabled at a virtual host level
//  - making a request to an unknown cluster
//  - request fails
TEST_P(OdCdsIntegrationTest, DisablingOdCdsAtVirtualHostLevelWorks) {
  doCleanUpXdsConnection_ = false;
  addOnDemandConfig(OdCdsIntegrationHelper::createOnDemandConfig(
      OdCdsIntegrationHelper::createOdCdsConfigSource("cluster_0"), 2500));
  addPerRouteConfig(OdCdsIntegrationHelper::PerRouteConfig(), "integration", {});
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.first"},
                                                 {"Pick-This-Cluster", "new_cluster"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);

  EXPECT_FALSE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, xds_connection_,
                                                         std::chrono::milliseconds(1000)));

  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "503", {}, {});

  cleanupUpstreamAndDownstream();
}

class OdCdsAdsIntegrationTest : public AdsIntegrationTest {
public:
  void initialize() override {
    AdsIntegrationTest::initialize();

    test_server_->waitUntilListenersReady();
    fake_upstream_idx_ = fake_upstreams_.size();
    auto& upstream = addFakeUpstream(FakeHttpConnection::Type::HTTP2);
    new_cluster_ =
        ConfigHelper::buildStaticCluster("new_cluster", upstream.localAddress()->ip()->port(),
                                         Network::Test::getLoopbackAddressString(ipVersion()));
  }

  envoy::config::listener::v3::Listener buildListener() {
    OdCdsListenerBuilder builder(Network::Test::getLoopbackAddressString(ipVersion()));
    auto ads_config_source = OdCdsIntegrationHelper::createAdsOdCdsConfigSource();
    auto per_route_config =
        OdCdsIntegrationHelper::createPerRouteConfig(std::move(ads_config_source), 2500);
    OdCdsIntegrationHelper::addPerRouteConfig(builder.hcm(), std::move(per_route_config),
                                              "integration", {});
    return builder.listener();
  }

  bool compareRequest(const std::string& type_url,
                      const std::vector<std::string>& expected_resource_subscriptions,
                      const std::vector<std::string>& expected_resource_unsubscriptions,
                      bool expect_node = false) {
    return compareDeltaDiscoveryRequest(type_url, expected_resource_subscriptions,
                                        expected_resource_unsubscriptions,
                                        Grpc::Status::WellKnownGrpcStatus::Ok, "", expect_node);
  };

  void doInitialCommunications() {
    // initial cluster query
    EXPECT_TRUE(compareRequest(Config::TypeUrl::get().Cluster, {}, {}, true));
    sendDeltaDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster,
                                                                    {}, {}, "1");

    // initial listener query
    EXPECT_TRUE(compareRequest(Config::TypeUrl::get().Listener, {}, {}));
    auto odcds_listener = buildListener();
    sendDeltaDiscoveryResponse<envoy::config::listener::v3::Listener>(
        Config::TypeUrl::get().Listener, {odcds_listener}, {}, "2");

    // acks
    EXPECT_TRUE(compareRequest(Config::TypeUrl::get().Cluster, {}, {}));
    EXPECT_TRUE(compareRequest(Config::TypeUrl::get().Listener, {}, {}));

    // listener got acked, so register the http port now.
    test_server_->waitUntilListenersReady();
    registerTestServerPorts({"http"});
  }

  std::size_t fake_upstream_idx_;
  envoy::config::cluster::v3::Cluster new_cluster_;
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientTypeDeltaWildcard, OdCdsAdsIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::ValuesIn(TestEnvironment::getsGrpcVersionsForTest()),
                     // Only delta xDS is supported for on-demand CDS.
                     testing::Values(Grpc::SotwOrDelta::Delta, Grpc::SotwOrDelta::UnifiedDelta),
                     // Only new DSS is supported for on-demand CDS.
                     testing::Values(OldDssOrNewDss::New)));

// tests a scenario when:
//  - making a request to an unknown cluster
//  - odcds initiates a connection with a request for the cluster
//  - a response contains the cluster
//  - request is resumed
TEST_P(OdCdsAdsIntegrationTest, OnDemandClusterDiscoveryWorksWithClusterHeader) {
  initialize();
  doInitialCommunications();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.first"},
                                                 {"Pick-This-Cluster", "new_cluster"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);

  EXPECT_TRUE(compareRequest(Config::TypeUrl::get().Cluster, {"new_cluster"}, {}));
  sendDeltaDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster,
                                                                  {new_cluster_}, {}, "3");
  EXPECT_TRUE(compareRequest(Config::TypeUrl::get().Cluster, {}, {}));

  waitForNextUpstreamRequest(fake_upstream_idx_);
  // Send response headers, and end_stream if there is no response body.
  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "200", {}, {});

  cleanupUpstreamAndDownstream();
}

// tests a scenario when:
//  - making a request to an unknown cluster
//  - odcds initiates a connection with a request for the cluster
//  - waiting for response times out
//  - request is resumed
TEST_P(OdCdsAdsIntegrationTest, OnDemandClusterDiscoveryTimesOut) {
  initialize();
  doInitialCommunications();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.first"},
                                                 {"Pick-This-Cluster", "new_cluster"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);

  EXPECT_TRUE(compareRequest(Config::TypeUrl::get().Cluster, {"new_cluster"}, {}));
  // not sending a response

  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "503", {}, {});

  cleanupUpstreamAndDownstream();
}

// tests a scenario when:
//  - making a request to an unknown cluster
//  - odcds initiates a connection with a request for the cluster
//  - a response says that there is no such cluster
//  - request is resumed
TEST_P(OdCdsAdsIntegrationTest, OnDemandClusterDiscoveryAsksForNonexistentCluster) {
  initialize();
  doInitialCommunications();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.first"},
                                                 {"Pick-This-Cluster", "new_cluster"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);

  EXPECT_TRUE(compareRequest(Config::TypeUrl::get().Cluster, {"new_cluster"}, {}));
  sendDeltaDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster,
                                                                  {}, {"new_cluster"}, "3");
  EXPECT_TRUE(compareRequest(Config::TypeUrl::get().Cluster, {}, {}));

  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "503", {}, {});

  cleanupUpstreamAndDownstream();
}

} // namespace
} // namespace Envoy
