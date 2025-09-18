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
#include "test/integration/ads_xdstp_config_sources_integration.h"
#include "test/integration/fake_upstream.h"
#include "test/integration/http_integration.h"
#include "test/integration/scoped_rds.h"
#include "test/integration/xdstp_config_sources_integration.h"
#include "test/test_common/resources.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class OdCdsIntegrationHelper {
public:
  using OnDemandCdsConfig = envoy::extensions::filters::http::on_demand::v3::OnDemandCds;
  using OnDemandConfig = envoy::extensions::filters::http::on_demand::v3::OnDemand;
  using PerRouteConfig = envoy::extensions::filters::http::on_demand::v3::PerRouteConfig;

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

  // Get the config, with a static listener. Uses ConfigHelper::baseConfigNoListener() together with
  // OdCdsIntegrationHelper::listenerConfig().
  static std::string bootstrapConfig() {
    return absl::StrCat(ConfigHelper::baseConfigNoListeners(),
                        "  listeners:", listenerConfig("127.0.0.1"));
  }

  static envoy::config::core::v3::ConfigSource
  createOdCdsConfigSource(absl::string_view cluster_name) {
    envoy::config::core::v3::ConfigSource source;
    TestUtility::loadFromYaml(fmt::format(R"EOF(
      api_config_source:
        api_type: DELTA_GRPC
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
      ads: {}
)EOF",
                              source);
    return source;
  }

  static OnDemandCdsConfig
  createOnDemandCdsConfig(absl::optional<envoy::config::core::v3::ConfigSource> config_source,
                          int timeout_millis) {
    OnDemandCdsConfig config;
    if (config_source.has_value()) {
      *config.mutable_source() = std::move(config_source.value());
    }
    *config.mutable_timeout() = ProtobufUtil::TimeUtil::MillisecondsToDuration(timeout_millis);
    return config;
  }

  template <typename OnDemandConfigType>
  static OnDemandConfigType
  createConfig(absl::optional<envoy::config::core::v3::ConfigSource> config_source,
               int timeout_millis) {
    OnDemandConfigType on_demand;
    *on_demand.mutable_odcds() = createOnDemandCdsConfig(std::move(config_source), timeout_millis);
    return on_demand;
  }

  static OnDemandConfig
  createOnDemandConfig(absl::optional<envoy::config::core::v3::ConfigSource> config_source,
                       int timeout_millis) {
    return createConfig<OnDemandConfig>(std::move(config_source), timeout_millis);
  }

  static PerRouteConfig
  createPerRouteConfig(absl::optional<envoy::config::core::v3::ConfigSource> config_source,
                       int timeout_millis) {
    return createConfig<PerRouteConfig>(std::move(config_source), timeout_millis);
  }

  static OptRef<Protobuf::Map<std::string, Protobuf::Any>>
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
  Protobuf::Any* hcm_any_;
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

  void initialize() override {
    // create_xds_upstream_ will create a fake upstream for odcds_cluster
    setUpstreamCount(0);
    // We want to have xds upstream available through xds_upstream_
    odcds_upstream_idx_ = 0;
    create_xds_upstream_ = true;
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Change cluster_0 to serve on-demand CDS.
      auto* odcds_cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      odcds_cluster->set_name("odcds_cluster");
      ConfigHelper::setHttp2(*odcds_cluster);
    });
    HttpIntegrationTest::initialize();

    // Create an upstream for the cluster returned by ODCDS. Needs to be called after initialize to
    // avoid asserts around port setup in BaseIntegrationTest.
    new_cluster_upstream_idx_ = fake_upstreams_.size();
    addFakeUpstream(Http::CodecType::HTTP2);
    new_cluster_ = ConfigHelper::buildStaticCluster(
        "new_cluster", fake_upstreams_[new_cluster_upstream_idx_]->localAddress()->ip()->port(),
        Network::Test::getLoopbackAddressString(ipVersion()));

    test_server_->waitUntilListenersReady();
    registerTestServerPorts({"http"});
  }

  FakeStreamPtr odcds_stream_;
  std::size_t odcds_upstream_idx_;
  std::size_t new_cluster_upstream_idx_;
  envoy::config::cluster::v3::Cluster new_cluster_;
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
                        OdCdsIntegrationHelper::createOdCdsConfigSource("odcds_cluster"), 2500),
                    "integration", {});
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.first"},
                                                 {"Pick-This-Cluster", "new_cluster"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);

  createXdsConnection();
  auto result = xds_connection_->waitForNewStream(*dispatcher_, odcds_stream_);
  RELEASE_ASSERT(result, result.message());
  odcds_stream_->startGrpcStream();

  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().Cluster, {"new_cluster"}, {},
                                           odcds_stream_.get()));
  sendDeltaDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TestTypeUrl::get().Cluster, {new_cluster_}, {}, "1", odcds_stream_.get());
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().Cluster, {}, {},
                                           odcds_stream_.get()));

  waitForNextUpstreamRequest(new_cluster_upstream_idx_);
  // Send response headers, and end_stream if there is no response body.
  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "200", {}, {});

  cleanUpXdsConnection();
  cleanupUpstreamAndDownstream();
}

// tests a scenario when:
//  - making a request to an unknown cluster
//  - odcds initiates a connection with a request for the cluster
//  - a response contains the cluster
//  - request is resumed
//  - another request is sent to the same cluster
//  - no odcds happens, because the cluster is known
TEST_P(OdCdsIntegrationTest, OnDemandClusterDiscoveryRemembersDiscoveredCluster) {
  addPerRouteConfig(OdCdsIntegrationHelper::createPerRouteConfig(
                        OdCdsIntegrationHelper::createOdCdsConfigSource("odcds_cluster"), 2500),
                    "integration", {});
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.first"},
                                                 {"Pick-This-Cluster", "new_cluster"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);

  createXdsConnection();
  auto result = xds_connection_->waitForNewStream(*dispatcher_, odcds_stream_);
  RELEASE_ASSERT(result, result.message());
  odcds_stream_->startGrpcStream();

  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().Cluster, {"new_cluster"}, {},
                                           odcds_stream_.get()));
  sendDeltaDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TestTypeUrl::get().Cluster, {new_cluster_}, {}, "1", odcds_stream_.get());
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().Cluster, {}, {},
                                           odcds_stream_.get()));

  waitForNextUpstreamRequest(new_cluster_upstream_idx_);
  // Send response headers, and end_stream if there is no response body.
  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "200", {}, {});

  // next request should be handled right away
  response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest(new_cluster_upstream_idx_);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "200", {}, {});

  cleanUpXdsConnection();
  cleanupUpstreamAndDownstream();
}

// tests a scenario when:
//  - making a request to an unknown cluster
//  - odcds initiates a connection with a request for the cluster
//  - no response happens, timeout is triggered
//  - request is resumed
TEST_P(OdCdsIntegrationTest, OnDemandClusterDiscoveryTimesOut) {
  addPerRouteConfig(OdCdsIntegrationHelper::createPerRouteConfig(
                        OdCdsIntegrationHelper::createOdCdsConfigSource("odcds_cluster"), 500),
                    "integration", {});
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.first"},
                                                 {"Pick-This-Cluster", "new_cluster"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);

  createXdsConnection();
  auto result = xds_connection_->waitForNewStream(*dispatcher_, odcds_stream_);
  RELEASE_ASSERT(result, result.message());
  odcds_stream_->startGrpcStream();

  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().Cluster, {"new_cluster"}, {},
                                           odcds_stream_.get()));
  // not sending a response to trigger the timeout

  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "503", {}, {});

  cleanUpXdsConnection();
  cleanupUpstreamAndDownstream();
}

// tests a scenario when:
//  - making a request to an unknown cluster
//  - odcds initiates a connection with a request for the cluster
//  - a response says the there is no such cluster
//  - request is resumed
TEST_P(OdCdsIntegrationTest, OnDemandClusterDiscoveryForNonexistentCluster) {
  addPerRouteConfig(OdCdsIntegrationHelper::createPerRouteConfig(
                        OdCdsIntegrationHelper::createOdCdsConfigSource("odcds_cluster"), 2500),
                    "integration", {});
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.first"},
                                                 {"Pick-This-Cluster", "new_cluster"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);

  createXdsConnection();
  auto result = xds_connection_->waitForNewStream(*dispatcher_, odcds_stream_);
  RELEASE_ASSERT(result, result.message());
  odcds_stream_->startGrpcStream();

  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().Cluster, {"new_cluster"}, {},
                                           odcds_stream_.get()));
  sendDeltaDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TestTypeUrl::get().Cluster, {}, {"new_cluster"}, "1", odcds_stream_.get());
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().Cluster, {}, {},
                                           odcds_stream_.get()));

  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "503", {}, {});

  cleanUpXdsConnection();
  cleanupUpstreamAndDownstream();
}

// tests a scenario when:
//  - ODCDS is enabled at a virtual host level
//  - ODCDS is disabled at a route level
//  - making a request to an unknown cluster
//  - request fails
TEST_P(OdCdsIntegrationTest, DisablingOdCdsAtRouteLevelWorks) {
  addPerRouteConfig(OdCdsIntegrationHelper::createPerRouteConfig(
                        OdCdsIntegrationHelper::createOdCdsConfigSource("odcds_cluster"), 2500),
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

  EXPECT_FALSE(fake_upstreams_[odcds_upstream_idx_]->waitForHttpConnection(
      *dispatcher_, xds_connection_, std::chrono::milliseconds(1000)));

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
  addOnDemandConfig(OdCdsIntegrationHelper::createOnDemandConfig(
      OdCdsIntegrationHelper::createOdCdsConfigSource("odcds_cluster"), 2500));
  addPerRouteConfig(OdCdsIntegrationHelper::PerRouteConfig(), "integration", {});
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.first"},
                                                 {"Pick-This-Cluster", "new_cluster"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);

  EXPECT_FALSE(fake_upstreams_[odcds_upstream_idx_]->waitForHttpConnection(
      *dispatcher_, xds_connection_, std::chrono::milliseconds(1000)));

  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "503", {}, {});

  cleanupUpstreamAndDownstream();
}

class OdCdsAdsIntegrationTest : public AdsIntegrationTest {
public:
  void initialize() override {
    AdsIntegrationTest::initialize();

    test_server_->waitUntilListenersReady();
    new_cluster_upstream_idx_ = fake_upstreams_.size();
    addFakeUpstream(Http::CodecType::HTTP2);
    new_cluster_ = ConfigHelper::buildStaticCluster(
        "new_cluster", fake_upstreams_[new_cluster_upstream_idx_]->localAddress()->ip()->port(),
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
    EXPECT_TRUE(compareRequest(Config::TestTypeUrl::get().Cluster, {}, {}, true));
    sendDeltaDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
        Config::TestTypeUrl::get().Cluster, {}, {}, "1");

    // initial listener query
    EXPECT_TRUE(compareRequest(Config::TestTypeUrl::get().Listener, {}, {}));
    auto odcds_listener = buildListener();
    sendDeltaDiscoveryResponse<envoy::config::listener::v3::Listener>(
        Config::TestTypeUrl::get().Listener, {odcds_listener}, {}, "2");

    // acks
    EXPECT_TRUE(compareRequest(Config::TestTypeUrl::get().Cluster, {}, {}));
    EXPECT_TRUE(compareRequest(Config::TestTypeUrl::get().Listener, {}, {}));

    // listener got acked, so register the http port now.
    test_server_->waitUntilListenersReady();
    registerTestServerPorts({"http"});
  }

  std::size_t new_cluster_upstream_idx_;
  envoy::config::cluster::v3::Cluster new_cluster_;
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientTypeDeltaWildcard, OdCdsAdsIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::ValuesIn(TestEnvironment::getsGrpcVersionsForTest()),
                     // Only delta xDS is supported for on-demand CDS.
                     testing::Values(Grpc::SotwOrDelta::Delta, Grpc::SotwOrDelta::UnifiedDelta)));

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

  EXPECT_TRUE(compareRequest(Config::TestTypeUrl::get().Cluster, {"new_cluster"}, {}));
  sendDeltaDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TestTypeUrl::get().Cluster, {new_cluster_}, {}, "3");
  EXPECT_TRUE(compareRequest(Config::TestTypeUrl::get().Cluster, {}, {}));

  waitForNextUpstreamRequest(new_cluster_upstream_idx_);
  // Send response headers, and end_stream if there is no response body.
  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "200", {}, {});

  cleanupUpstreamAndDownstream();
}

// tests a scenario when:
//  - making a request to an unknown cluster
//  - odcds initiates a connection with a request for the cluster
//  - a response contains the cluster
//  - request is resumed
//  - another request is sent to the same cluster
//  - no odcds happens, because the cluster is known
TEST_P(OdCdsAdsIntegrationTest, OnDemandClusterDiscoveryRemembersDiscoveredCluster) {
  initialize();
  doInitialCommunications();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.first"},
                                                 {"Pick-This-Cluster", "new_cluster"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);

  EXPECT_TRUE(compareRequest(Config::TestTypeUrl::get().Cluster, {"new_cluster"}, {}));
  sendDeltaDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TestTypeUrl::get().Cluster, {new_cluster_}, {}, "3");
  EXPECT_TRUE(compareRequest(Config::TestTypeUrl::get().Cluster, {}, {}));

  waitForNextUpstreamRequest(new_cluster_upstream_idx_);
  // Send response headers, and end_stream if there is no response body.
  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "200", {}, {});

  // next request should be handled right away
  response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest(new_cluster_upstream_idx_);
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

  EXPECT_TRUE(compareRequest(Config::TestTypeUrl::get().Cluster, {"new_cluster"}, {}));
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

  EXPECT_TRUE(compareRequest(Config::TestTypeUrl::get().Cluster, {"new_cluster"}, {}));
  sendDeltaDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TestTypeUrl::get().Cluster, {}, {"new_cluster"}, "3");
  EXPECT_TRUE(compareRequest(Config::TestTypeUrl::get().Cluster, {}, {}));

  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "503", {}, {});

  cleanupUpstreamAndDownstream();
}

class OdCdsXdstpIntegrationTest : public XdsTpConfigsIntegration {
public:
  void initialize() override {
    // Skipping port usage validation because this tests will create new clusters
    // that will be sent to the OD-CDS subscriptions.
    config_helper_.skipPortUsageValidation();

    // Set up the listener and add the PerRouteConfig in it that will have the
    // ODCDS filter.
    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          auto* static_resources = bootstrap.mutable_static_resources();
          // Replace the listener.
          *static_resources->mutable_listeners(0) = buildListener();
        });

    // Envoy will only connect to the xDS-TP servers that are defined in the
    // bootstrap, but won't issue a subscription yet.
    on_server_init_function_ = [this]() {
      connectAuthority1();
      connectDefaultAuthority();
    };
    XdsTpConfigsIntegration::initialize();

    test_server_->waitUntilListenersReady();
    // Add a fake cluster server that will be returned for the OD-CDS request.
    new_cluster_upstream_idx_ = fake_upstreams_.size();
    addFakeUpstream(Http::CodecType::HTTP2);
    new_cluster_ = ConfigHelper::buildStaticCluster(
        "xdstp://authority1.com/envoy.config.cluster.v3.Cluster/on_demand_clusters/new_cluster",
        fake_upstreams_[new_cluster_upstream_idx_]->localAddress()->ip()->port(),
        Network::Test::getLoopbackAddressString(ipVersion()));
    registerTestServerPorts({"http"});
  }

  envoy::config::listener::v3::Listener buildListener() {
    OdCdsListenerBuilder builder(Network::Test::getLoopbackAddressString(ipVersion()));
    auto per_route_config = OdCdsIntegrationHelper::createPerRouteConfig(absl::nullopt, 2500);
    OdCdsIntegrationHelper::addPerRouteConfig(builder.hcm(), std::move(per_route_config),
                                              "integration", {});
    return builder.listener();
  }

  envoy::config::endpoint::v3::ClusterLoadAssignment
  buildClusterLoadAssignment(const std::string& name, size_t upstream_idx) {
    return ConfigHelper::buildClusterLoadAssignment(
        name, Network::Test::getLoopbackAddressString(ipVersion()),
        fake_upstreams_[upstream_idx]->localAddress()->ip()->port());
  }

  bool compareRequest(const std::string& type_url,
                      const std::vector<std::string>& expected_resource_subscriptions,
                      const std::vector<std::string>& expected_resource_unsubscriptions,
                      bool expect_node = false) {
    return compareDeltaDiscoveryRequest(type_url, expected_resource_subscriptions,
                                        expected_resource_unsubscriptions,
                                        Grpc::Status::WellKnownGrpcStatus::Ok, "", expect_node);
  };

  std::size_t new_cluster_upstream_idx_;
  envoy::config::cluster::v3::Cluster new_cluster_;
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientTypeDeltaWildcard, OdCdsXdstpIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::ValuesIn(TestEnvironment::getsGrpcVersionsForTest()),
                     // TODO(adisuissa): add SotW validation - this should work
                     // as long as there isn't both empty wildcard and on-demand
                     // on the same xds-tp gRPC-mux (which is not supported at
                     // the moment).
                     // Only delta xDS is supported for on-demand CDS.
                     testing::Values(Grpc::SotwOrDelta::Delta, Grpc::SotwOrDelta::UnifiedDelta)));

// tests a scenario when:
//  - making a request to an unknown cluster
//  - odcds initiates a connection with a request for the cluster
//  - a response contains the cluster
//  - request is resumed
TEST_P(OdCdsXdstpIntegrationTest, OnDemandClusterDiscoveryWorksWithClusterHeader) {
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  const std::string& cluster_name = new_cluster_.name();
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.first"},
                                                 {"Pick-This-Cluster", cluster_name}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);

  // Authority1 should receive the ODCDS request.
  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TestTypeUrl::get().Cluster, "", {cluster_name}, {cluster_name}, {}, true,
      Grpc::Status::WellKnownGrpcStatus::Ok, "", authority1_xds_stream_.get()));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TestTypeUrl::get().Cluster,
                                                             {new_cluster_}, {new_cluster_}, {},
                                                             "1", {}, authority1_xds_stream_.get());
  // Expect a CDS ACK from authority1.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "1", {cluster_name}, {},
                                      {}, false, Grpc::Status::WellKnownGrpcStatus::Ok, "",
                                      authority1_xds_stream_.get()));

  waitForNextUpstreamRequest(new_cluster_upstream_idx_);
  // Send response headers, and end_stream if there is no response body.
  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "200", {}, {});

  cleanupUpstreamAndDownstream();
}

// tests a scenario when:
//  - making a request to an unknown cluster
//  - odcds initiates a connection with a request for the cluster
//  - a response contains the cluster
//  - request is resumed
//  - another request is sent to the same cluster
//  - no odcds happens, because the cluster is known
TEST_P(OdCdsXdstpIntegrationTest, OnDemandClusterDiscoveryRemembersDiscoveredCluster) {
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  const std::string& cluster_name = new_cluster_.name();
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.first"},
                                                 {"Pick-This-Cluster", cluster_name}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);

  // Authority1 should receive the ODCDS request.
  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TestTypeUrl::get().Cluster, "", {cluster_name}, {cluster_name}, {}, true,
      Grpc::Status::WellKnownGrpcStatus::Ok, "", authority1_xds_stream_.get()));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TestTypeUrl::get().Cluster,
                                                             {new_cluster_}, {new_cluster_}, {},
                                                             "1", {}, authority1_xds_stream_.get());
  // Expect a CDS ACK from authority1.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "1", {cluster_name}, {},
                                      {}, false, Grpc::Status::WellKnownGrpcStatus::Ok, "",
                                      authority1_xds_stream_.get()));

  waitForNextUpstreamRequest(new_cluster_upstream_idx_);
  // Send response headers, and end_stream if there is no response body.
  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "200", {}, {});

  // Next request should be handled right away (no xDS subscription).
  response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest(new_cluster_upstream_idx_);
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
TEST_P(OdCdsXdstpIntegrationTest, OnDemandClusterDiscoveryTimesOut) {
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  const std::string& cluster_name = new_cluster_.name();
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.first"},
                                                 {"Pick-This-Cluster", cluster_name}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);

  // Authority1 should receive the ODCDS request.
  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TestTypeUrl::get().Cluster, "", {cluster_name}, {cluster_name}, {}, true,
      Grpc::Status::WellKnownGrpcStatus::Ok, "", authority1_xds_stream_.get()));
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
TEST_P(OdCdsXdstpIntegrationTest, OnDemandClusterDiscoveryAsksForNonexistentCluster) {
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  const std::string& cluster_name = new_cluster_.name();
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.first"},
                                                 {"Pick-This-Cluster", cluster_name}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);

  // Authority1 should receive the ODCDS request.
  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TestTypeUrl::get().Cluster, "", {cluster_name}, {cluster_name}, {}, true,
      Grpc::Status::WellKnownGrpcStatus::Ok, "", authority1_xds_stream_.get()));
  // Send a response to remove the requested cluster (not found).
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TestTypeUrl::get().Cluster, {},
                                                             {}, {cluster_name}, "1", {},
                                                             authority1_xds_stream_.get());
  // Expect a CDS ACK from authority1.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "1", {cluster_name}, {},
                                      {}, false, Grpc::Status::WellKnownGrpcStatus::Ok, "",
                                      authority1_xds_stream_.get()));

  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "503", {}, {});

  cleanupUpstreamAndDownstream();
}

// tests a scenario when:
//  - making a request to an unknown cluster
//  - odcds initiates a connection with a request for the cluster
//  - a response contains an EDS cluster
//  - an EDS request is sent to the same authority
//  - an EDS response is received
//  - request is resumed
TEST_P(OdCdsXdstpIntegrationTest, OnDemandCdsWithEds) {
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  const std::string cds_cluster_name =
      "xdstp://authority1.com/envoy.config.cluster.v3.Cluster/on_demand_clusters/"
      "new_cluster_with_eds";
  const std::string eds_service_name =
      "xdstp://authority1.com/envoy.config.endpoint.v3.ClusterLoadAssignment/on_demand_clusters/"
      "new_cluster_with_eds";

  envoy::config::cluster::v3::Cluster new_cluster_with_eds;
  new_cluster_with_eds.set_name(cds_cluster_name);
  new_cluster_with_eds.set_type(envoy::config::cluster::v3::Cluster::EDS);
  auto* eds_cluster_config = new_cluster_with_eds.mutable_eds_cluster_config();
  eds_cluster_config->set_service_name(eds_service_name);
  ConfigHelper::setHttp2(new_cluster_with_eds);

  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.first"},
                                                 {"Pick-This-Cluster", cds_cluster_name}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);

  // Authority1 should receive the ODCDS request.
  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TestTypeUrl::get().Cluster, "", {cds_cluster_name}, {cds_cluster_name}, {}, true,
      Grpc::Status::WellKnownGrpcStatus::Ok, "", authority1_xds_stream_.get()));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TestTypeUrl::get().Cluster, {new_cluster_with_eds}, {new_cluster_with_eds}, {}, "1",
      {}, authority1_xds_stream_.get());
  // After the CDS response, Envoy will send an EDS request for the new cluster.
  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TestTypeUrl::get().ClusterLoadAssignment, "", {eds_service_name}, {eds_service_name},
      {}, false, Grpc::Status::WellKnownGrpcStatus::Ok, "", authority1_xds_stream_.get()));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TestTypeUrl::get().ClusterLoadAssignment,
      {buildClusterLoadAssignment(eds_service_name, new_cluster_upstream_idx_)},
      {buildClusterLoadAssignment(eds_service_name, new_cluster_upstream_idx_)}, {}, "2", {},
      authority1_xds_stream_.get());
  // Now, Envoy should ACK the original CDS response.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "1", {cds_cluster_name},
                                      {}, {}, false, Grpc::Status::WellKnownGrpcStatus::Ok, "",
                                      authority1_xds_stream_.get()));
  // And finally, Envoy should ACK the EDS response.
  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TestTypeUrl::get().ClusterLoadAssignment, "2", {eds_service_name}, {}, {}, false,
      Grpc::Status::WellKnownGrpcStatus::Ok, "", authority1_xds_stream_.get()));

  waitForNextUpstreamRequest(new_cluster_upstream_idx_);
  // Send response headers, and end_stream if there is no response body.
  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "200", {}, {});

  cleanupUpstreamAndDownstream();
}

/**
 * Tests a use-case where OD-CDS is using xDS-TP based config source, and an
 * (old) ADS source updates the wildcard clusters subscriptions.
 */
class OdCdsXdstpAdsIntegrationTest : public AdsXdsTpConfigsIntegrationTest {
public:
  OdCdsXdstpAdsIntegrationTest() : AdsXdsTpConfigsIntegrationTest() {
    // Override the sotw_or_delta_ settings to only use SotW-ADS.
    // Note that in the future this can be modified to support other types as
    // well, but currently not needed.
    ads_config_type_override_ = envoy::config::core::v3::ApiConfigSource::GRPC;
  }

  void initialize() override {
    // Skipping port usage validation because this tests will create new clusters
    // that will be sent to the OD-CDS subscriptions.
    config_helper_.skipPortUsageValidation();
    AdsXdsTpConfigsIntegrationTest::initialize();

    // Add a fake cluster server that will be returned for the OD-CDS request.
    new_cluster_upstream_idx_ = fake_upstreams_.size();
    addFakeUpstream(Http::CodecType::HTTP2);
    new_cluster_ = ConfigHelper::buildStaticCluster(
        "xdstp://authority1.com/envoy.config.cluster.v3.Cluster/on_demand_clusters/new_cluster",
        fake_upstreams_[new_cluster_upstream_idx_]->localAddress()->ip()->port(),
        Network::Test::getLoopbackAddressString(ipVersion()));
  }

  envoy::config::listener::v3::Listener buildListener() {
    OdCdsListenerBuilder builder(Network::Test::getLoopbackAddressString(ipVersion()));
    auto per_route_config = OdCdsIntegrationHelper::createPerRouteConfig(absl::nullopt, 2500);
    OdCdsIntegrationHelper::addPerRouteConfig(builder.hcm(), std::move(per_route_config),
                                              "integration", {});
    return builder.listener();
  }

  envoy::config::endpoint::v3::ClusterLoadAssignment
  buildClusterLoadAssignment(const std::string& name, size_t upstream_idx) {
    return ConfigHelper::buildClusterLoadAssignment(
        name, Network::Test::getLoopbackAddressString(ipVersion()),
        fake_upstreams_[upstream_idx]->localAddress()->ip()->port());
  }

  bool compareRequest(const std::string& type_url,
                      const std::vector<std::string>& expected_resource_subscriptions,
                      const std::vector<std::string>& expected_resource_unsubscriptions,
                      bool expect_node = false) {
    return compareDeltaDiscoveryRequest(type_url, expected_resource_subscriptions,
                                        expected_resource_unsubscriptions,
                                        Grpc::Status::WellKnownGrpcStatus::Ok, "", expect_node);
  };

  // Compares a discovery request from the (old) ADS stream. This only supports
  // SotW at the moment.
  AssertionResult compareAdsDiscoveryRequest(
      const std::string& expected_type_url, const std::string& expected_version,
      const std::vector<std::string>& expected_resource_names, bool expect_node = false,
      const Protobuf::int32 expected_error_code = Grpc::Status::WellKnownGrpcStatus::Ok,
      const std::string& expected_error_substring = "") {
    return compareSotwDiscoveryRequest(expected_type_url, expected_version, expected_resource_names,
                                       expect_node, expected_error_code, expected_error_substring);
  }

  // Sends a discovery response using the (old) ADS stream. This only supports
  // SotW at the moment.
  template <class T>
  void sendAdsDiscoveryResponse(const std::string& type_url,
                                const std::vector<T>& state_of_the_world,
                                const std::string& version) {
    sendSotwDiscoveryResponse(type_url, state_of_the_world, version);
  }

  std::size_t new_cluster_upstream_idx_;
  envoy::config::cluster::v3::Cluster new_cluster_;
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientTypeDeltaWildcard, OdCdsXdstpAdsIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::ValuesIn(TestEnvironment::getsGrpcVersionsForTest()),
                     // TODO(adisuissa): add SotW validation - this should work
                     // as long as there isn't both empty wildcard and on-demand
                     // on the same xds-tp gRPC-mux (which is not supported at
                     // the moment).
                     // Only delta xDS is supported for on-demand CDS.
                     testing::Values(Grpc::SotwOrDelta::Delta, Grpc::SotwOrDelta::UnifiedDelta)));

// tests a scenario when:
//  - Envoy receives a CDS over SotW-ADS update, and receives 1 cluster
//  - downstream client makes a request to an unknown cluster
//  - odcds initiates a connection with a request for the cluster
//  - a response contains the cluster
//  - request is resumed
//  - Envoy receives an update to the CDS over SotW-ADS
//  - another request is sent to the same on-demand cluster
//  - no odcds happens, because the cluster is known, and the request is successful
TEST_P(OdCdsXdstpAdsIntegrationTest, OnDemandClusterDiscoveryWithSotwAds) {
  // Sets the cds_config (lds is needed to allow proper integration test suite initialization).
  setupClustersFromOldAds();
  setupListenersFromOldAds();
  initialize();

  // Handle the CDS request - send a single cluster.
  // Wait for ADS clusters request and send a cluster that points to load
  // assignment in authority1.com.
  EXPECT_TRUE(compareAdsDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "", {}, true));
  envoy::config::cluster::v3::Cluster sotw_cluster = ConfigHelper::buildStaticCluster(
      "sotw_cluster", 1234, Network::Test::getLoopbackAddressString(ipVersion()));
  sendAdsDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TestTypeUrl::get().Cluster,
                                                                {sotw_cluster}, "1");

  // Send the Listener (with the OD-CDS filter) using the old ADS.
  EXPECT_TRUE(compareAdsDiscoveryRequest(Config::TestTypeUrl::get().Listener, "", {}));
  const envoy::config::listener::v3::Listener listener = buildListener();
  sendAdsDiscoveryResponse<envoy::config::listener::v3::Listener>(
      Config::TestTypeUrl::get().Listener, {listener}, "1");

  // Old ADS receives a CDS and a LDS ACK.
  EXPECT_TRUE(compareAdsDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "1", {}));
  EXPECT_TRUE(compareAdsDiscoveryRequest(Config::TestTypeUrl::get().Listener, "1", {}));
  // Expected 5 clusters: dummy, authority1_cluster, default_authority_cluster,
  // ads_cluster and sotw_cluster.
  EXPECT_EQ(5, test_server_->gauge("cluster_manager.active_clusters")->value());

  // Envoy should now complete initialization.
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 1);
  registerTestServerPorts({"http"});

  // Send the first request.
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  const std::string& cluster_name = new_cluster_.name();
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "vhost.first"},
                                                 {"Pick-This-Cluster", cluster_name}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);

  // Authority1 should receive the ODCDS request.
  EXPECT_TRUE(compareDiscoveryRequest(
      Config::TestTypeUrl::get().Cluster, "", {cluster_name}, {cluster_name}, {}, true,
      Grpc::Status::WellKnownGrpcStatus::Ok, "", authority1_xds_stream_.get()));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TestTypeUrl::get().Cluster,
                                                             {new_cluster_}, {new_cluster_}, {},
                                                             "1", {}, authority1_xds_stream_.get());
  // Expect a CDS ACK from authority1.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "1", {cluster_name}, {},
                                      {}, false, Grpc::Status::WellKnownGrpcStatus::Ok, "",
                                      authority1_xds_stream_.get()));

  waitForNextUpstreamRequest(new_cluster_upstream_idx_);
  // Send response headers, and end_stream if there is no response body.
  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "200", {}, {});

  // Expected 6 clusters: dummy, authority1_cluster, default_authority_cluster,
  // ads_cluster, sotw_cluster, and the OD-CDS-cluster.
  EXPECT_EQ(6, test_server_->gauge("cluster_manager.active_clusters")->value());

  // Update the SotW cluster, and send it.
  sotw_cluster.mutable_connect_timeout()->set_seconds(5);
  sendAdsDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TestTypeUrl::get().Cluster,
                                                                {sotw_cluster}, "2");
  // Old ADS receives a CDS ACK.
  EXPECT_TRUE(compareAdsDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "2", {}));
  // Expected 6 clusters: dummy, authority1_cluster, default_authority_cluster,
  // ads_cluster, sotw_cluster, and the OD-CDS-cluster.
  EXPECT_EQ(6, test_server_->gauge("cluster_manager.active_clusters")->value());

  // Next request should be handled right away (no xDS subscription).
  response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest(new_cluster_upstream_idx_);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "200", {}, {});

  cleanupUpstreamAndDownstream();
}

class OdCdsScopedRdsIntegrationTestBase : public ScopedRdsIntegrationTest {
public:
  void addOnDemandConfig(OdCdsIntegrationHelper::OnDemandConfig config) {
    config_helper_.addConfigModifier(
        [config = std::move(config)](ConfigHelper::HttpConnectionManager& hcm) {
          OdCdsIntegrationHelper::addOnDemandConfig(hcm, std::move(config));
        });
  }

  void initialize() override {
    ScopedRdsIntegrationTest::setupModifications();
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* odcds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      odcds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      odcds_cluster->set_name("odcds_cluster");
      ConfigHelper::setHttp2(*odcds_cluster);
    });
    on_server_init_function_ = [this]() {
      const std::string scope_route1 = R"EOF(
name: foo_scope1
route_configuration_name: foo_route1
on_demand: true
key:
  fragments:
    - string_key: foo
)EOF";
      createScopedRdsStream();
      sendSrdsResponse({scope_route1}, {scope_route1}, {}, "1");
    };
    // We want to have odcds upstream available through xds_upstream_
    create_xds_upstream_ = true;
    ScopedRdsIntegrationTest::initialize();

    // We expect the odcds fake upstream to be the last one in fake_upstreams_ at the moment.
    odcds_upstream_idx_ = fake_upstreams_.size() - 1;
    // Create the new cluster upstream.
    new_cluster_upstream_idx_ = fake_upstreams_.size();
    addFakeUpstream(Http::CodecType::HTTP2);
    new_cluster_ = ConfigHelper::buildStaticCluster(
        "new_cluster", fake_upstreams_[new_cluster_upstream_idx_]->localAddress()->ip()->port(),
        Network::Test::getLoopbackAddressString(ipVersion()));

    test_server_->waitUntilListenersReady();
    registerTestServerPorts({"http"});
  }

  using RouteConfigFormatter = std::function<std::string(absl::string_view)>;
  IntegrationStreamDecoderPtr initialRDSCommunication(RouteConfigFormatter route_config_formatter) {
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
    // Request that matches lazily loaded scope will trigger on demand loading.
    auto response = codec_client_->makeHeaderOnlyRequest(
        Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                       {":path", "/meh"},
                                       {":scheme", "http"},
                                       {":authority", "vhost.first"},
                                       {"Pick-This-Cluster", "new_cluster"},
                                       {"Addr", "x-foo-key=foo"}});
    createRdsStream("foo_route1");
    sendRdsResponse(route_config_formatter("foo_route1"), "1");
    test_server_->waitForCounterGe("http.config_test.rds.foo_route1.update_success", 1);
    return response;
  }

  enum class VHostOdCdsConfig {
    None,
    Disable,
    Enable,
  };

  enum class RouteOdCdsConfig {
    None,
    Disable,
    Enable,
  };

  RouteConfigFormatter getRouteConfigFormatter(VHostOdCdsConfig vhost_config,
                                               RouteOdCdsConfig route_config) {
    RouteConfigFormatter formatter = [vhost_config, route_config](absl::string_view route_name) {
      static constexpr absl::string_view vhost_config_enabled = R"EOF(
          typed_per_filter_config:
            envoy.filters.http.on_demand:
              "@type": type.googleapis.com/envoy.extensions.filters.http.on_demand.v3.PerRouteConfig
              odcds:
                source:
                  api_config_source:
                    api_type: DELTA_GRPC
                    grpc_services:
                      envoy_grpc:
                        cluster_name: odcds_cluster
                timeout: "2.5s"
  )EOF";
      static constexpr absl::string_view vhost_config_disabled = R"EOF(
          typed_per_filter_config:
            envoy.filters.http.on_demand:
              "@type": type.googleapis.com/envoy.extensions.filters.http.on_demand.v3.PerRouteConfig
  )EOF";
      static constexpr absl::string_view route_config_enabled = R"EOF(
            typed_per_filter_config:
              envoy.filters.http.on_demand:
                "@type": type.googleapis.com/envoy.extensions.filters.http.on_demand.v3.PerRouteConfig
                odcds:
                  source:
                    api_config_source:
                      api_type: DELTA_GRPC
                      grpc_services:
                        envoy_grpc:
                          cluster_name: odcds_cluster
                  timeout: "2.5s"
  )EOF";
      static constexpr absl::string_view route_config_disabled = R"EOF(
            typed_per_filter_config:
              envoy.filters.http.on_demand:
                "@type": type.googleapis.com/envoy.extensions.filters.http.on_demand.v3.PerRouteConfig
  )EOF";
      absl::string_view picked_vhost_config;
      absl::string_view picked_route_config;
      switch (vhost_config) {
      case VHostOdCdsConfig::None:
        break;

      case VHostOdCdsConfig::Disable:
        picked_vhost_config = vhost_config_disabled;
        break;

      case VHostOdCdsConfig::Enable:
        picked_vhost_config = vhost_config_enabled;
        break;
      }

      switch (route_config) {
      case RouteOdCdsConfig::None:
        break;

      case RouteOdCdsConfig::Disable:
        picked_route_config = route_config_disabled;
        break;

      case RouteOdCdsConfig::Enable:
        picked_route_config = route_config_enabled;
        break;
      }

      return fmt::format(R"EOF(
        virtual_hosts:
        - name: integration
          {}
          routes:
          - name: odcds_route
            {}
            route:
              cluster_header: "Pick-This-Cluster"
            match:
              prefix: "/"
          domains: ["*"]
        name: {}
  )EOF",
                         picked_vhost_config, picked_route_config, route_name);
    };
    return formatter;
  }

  void serveOdCdsExpect200(IntegrationStreamDecoderPtr response) {
    createXdsConnection();
    auto result = xds_connection_->waitForNewStream(*dispatcher_, odcds_stream_);
    RELEASE_ASSERT(result, result.message());
    odcds_stream_->startGrpcStream();

    EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().Cluster, {"new_cluster"},
                                             {}, odcds_stream_.get()));
    sendDeltaDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
        Config::TestTypeUrl::get().Cluster, {new_cluster_}, {}, "1", odcds_stream_.get());
    EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TestTypeUrl::get().Cluster, {}, {},
                                             odcds_stream_.get()));

    waitForNextUpstreamRequest(new_cluster_upstream_idx_);
    // Send response headers, and end_stream if there is no response body.
    upstream_request_->encodeHeaders(default_response_headers_, true);

    ASSERT_TRUE(response->waitForEndStream());
    verifyResponse(std::move(response), "200", {}, {});

    cleanUpXdsConnection();
  }

  void noOdCdsExpect503(IntegrationStreamDecoderPtr response) {
    EXPECT_FALSE(fake_upstreams_[odcds_upstream_idx_]->waitForHttpConnection(
        *dispatcher_, xds_connection_, std::chrono::milliseconds(1000)));

    ASSERT_TRUE(response->waitForEndStream());
    verifyResponse(std::move(response), "503", {}, {});
  }

  FakeStreamPtr odcds_stream_;
  std::size_t odcds_upstream_idx_;
  std::size_t new_cluster_upstream_idx_;
  envoy::config::cluster::v3::Cluster new_cluster_;
};

using OdCdsScopedRdsIntegrationTest = OdCdsScopedRdsIntegrationTestBase;

INSTANTIATE_TEST_SUITE_P(IpVersionsAndGrpcTypes, OdCdsScopedRdsIntegrationTest,
                         DELTA_SOTW_GRPC_CLIENT_INTEGRATION_PARAMS);

// Test the an update of scoped route config is performed on demand. Since the config contains the
// cluster-header action and HCM config enables on demand cluster discovery, it kicks in too. After
// all this, the HTTP request should succeed.
TEST_P(OdCdsScopedRdsIntegrationTest, OnDemandUpdateSuccessRDSThenCDS) {
  config_helper_.prependFilter(R"EOF(
    name: envoy.filters.http.on_demand
    )EOF");
  addOnDemandConfig(OdCdsIntegrationHelper::createOnDemandConfig(
      OdCdsIntegrationHelper::createOdCdsConfigSource("odcds_cluster"), 2500));
  initialize();

  auto response = initialRDSCommunication(
      getRouteConfigFormatter(VHostOdCdsConfig::None, RouteOdCdsConfig::None));
  serveOdCdsExpect200(std::move(response));
  cleanupUpstreamAndDownstream();
}

// Test the an update of scoped route config is performed on demand. Since the config contains the
// cluster-header action and the scoped route config enables on demand cluster discovery on a vhost
// level, it kicks in too. After all this, the HTTP request should succeed.
TEST_P(OdCdsScopedRdsIntegrationTest, OnDemandUpdateSuccessRDSThenCDSInVHost) {
  config_helper_.prependFilter(R"EOF(
    name: envoy.filters.http.on_demand
    )EOF");
  initialize();

  auto response = initialRDSCommunication(
      getRouteConfigFormatter(VHostOdCdsConfig::Enable, RouteOdCdsConfig::None));
  serveOdCdsExpect200(std::move(response));
  cleanupUpstreamAndDownstream();
}

// Test the an update of scoped route config is performed on demand. Since the config contains the
// cluster-header action and the scoped route config enables on demand cluster discovery on a route
// level, it kicks in too. After all this, the HTTP request should succeed.
TEST_P(OdCdsScopedRdsIntegrationTest, OnDemandUpdateSuccessRDSThenCDSInRoute) {
  config_helper_.prependFilter(R"EOF(
    name: envoy.filters.http.on_demand
    )EOF");
  initialize();

  auto response = initialRDSCommunication(
      getRouteConfigFormatter(VHostOdCdsConfig::None, RouteOdCdsConfig::Enable));
  serveOdCdsExpect200(std::move(response));
  cleanupUpstreamAndDownstream();
}

// Test that an update of scoped route config is performed on demand. Despite the fact that config
// contains the cluster-header action, the request will fail, because ODCDS is not enabled.
TEST_P(OdCdsScopedRdsIntegrationTest, OnDemandUpdateFailsBecauseOdCdsIsDisabled) {
  config_helper_.prependFilter(R"EOF(
    name: envoy.filters.http.on_demand
    )EOF");
  initialize();

  auto response = initialRDSCommunication(
      getRouteConfigFormatter(VHostOdCdsConfig::None, RouteOdCdsConfig::None));
  noOdCdsExpect503(std::move(response));

  cleanupUpstreamAndDownstream();
}

// Test that an update of scoped route config is performed on demand. Despite the fact that config
// contains the cluster-header action and ODCDS is enabled in HCM, the request will fail, because
// ODCDS is disabled in virtual host.
TEST_P(OdCdsScopedRdsIntegrationTest, OnDemandUpdateFailsBecauseOdCdsIsDisabledInVHost) {
  config_helper_.prependFilter(R"EOF(
    name: envoy.filters.http.on_demand
    )EOF");
  addOnDemandConfig(OdCdsIntegrationHelper::createOnDemandConfig(
      OdCdsIntegrationHelper::createOdCdsConfigSource("odcds_cluster"), 2500));
  initialize();

  auto response = initialRDSCommunication(
      getRouteConfigFormatter(VHostOdCdsConfig::Disable, RouteOdCdsConfig::None));
  noOdCdsExpect503(std::move(response));
  cleanupUpstreamAndDownstream();
}

// Test that an update of scoped route config is performed on demand. Despite the fact that config
// contains the cluster-header action and ODCDS is enabled in HCM, the request will fail, because
// ODCDS is disabled in route.
TEST_P(OdCdsScopedRdsIntegrationTest, OnDemandUpdateFailsBecauseOdCdsIsDisabledInRoute) {
  config_helper_.prependFilter(R"EOF(
    name: envoy.filters.http.on_demand
    )EOF");
  addOnDemandConfig(OdCdsIntegrationHelper::createOnDemandConfig(
      OdCdsIntegrationHelper::createOdCdsConfigSource("odcds_cluster"), 2500));
  initialize();

  auto response = initialRDSCommunication(
      getRouteConfigFormatter(VHostOdCdsConfig::None, RouteOdCdsConfig::Disable));
  noOdCdsExpect503(std::move(response));

  cleanupUpstreamAndDownstream();
}

// Test that an update of scoped route config is performed on demand. Despite the fact that config
// contains the cluster-header action and ODCDS is enabled in vhost, the request will fail, because
// ODCDS is disabled in route.
TEST_P(OdCdsScopedRdsIntegrationTest, OnDemandUpdateFailsBecauseOdCdsIsDisabledInRoute2) {
  config_helper_.prependFilter(R"EOF(
    name: envoy.filters.http.on_demand
    )EOF");
  initialize();

  auto response = initialRDSCommunication(
      getRouteConfigFormatter(VHostOdCdsConfig::Enable, RouteOdCdsConfig::Disable));
  noOdCdsExpect503(std::move(response));

  cleanupUpstreamAndDownstream();
}
} // namespace
} // namespace Envoy
