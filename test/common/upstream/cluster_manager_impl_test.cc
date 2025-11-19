#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.validate.h"
#include "envoy/config/config_validator.h"
#include "envoy/config/core/v3/base.pb.h"

#include "source/common/config/null_grpc_mux_impl.h"
#include "source/common/config/xds_resource.h"
#include "source/common/network/raw_buffer_socket.h"
#include "source/common/network/resolver_impl.h"
#include "source/common/router/context_impl.h"
#include "source/extensions/transport_sockets/raw_buffer/config.h"

#include "test/common/quic/test_utils.h"
#include "test/common/upstream/cluster_manager_impl_test_common.h"
#include "test/common/upstream/test_cluster_manager.h"
#include "test/config/v2_link_hacks.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/http/conn_pool.h"
#include "test/mocks/matcher/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/upstream/cluster_priority_set.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/mocks/upstream/thread_aware_load_balancer.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Upstream {

namespace {

using ::envoy::config::bootstrap::v3::Bootstrap;
using ::Envoy::StatusHelpers::StatusCodeIs;
using ::testing::_;
using ::testing::DoAll;
using ::testing::HasSubstr;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::InvokeWithoutArgs;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnNew;
using ::testing::ReturnRef;
using ::testing::SaveArg;

using namespace std::chrono_literals;

void verifyCaresDnsConfigAndUnpack(
    const envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config,
    envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig& cares) {
  // Verify typed DNS resolver config is c-ares.
  EXPECT_EQ(typed_dns_resolver_config.name(), std::string(Network::CaresDnsResolver));
  EXPECT_EQ(
      typed_dns_resolver_config.typed_config().type_url(),
      "type.googleapis.com/envoy.extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig");
  typed_dns_resolver_config.typed_config().UnpackTo(&cares);
}

class AlpnSocketFactory : public Network::RawBufferSocketFactory {
public:
  bool supportsAlpn() const override { return true; }
};

class AlpnTestConfigFactory
    : public Envoy::Extensions::TransportSockets::RawBuffer::UpstreamRawBufferSocketFactory {
public:
  std::string name() const override { return "envoy.transport_sockets.alpn"; }
  absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr>
  createTransportSocketFactory(const Protobuf::Message&,
                               Server::Configuration::TransportSocketFactoryContext&) override {
    return std::make_unique<AlpnSocketFactory>();
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }
};

TEST_F(ClusterManagerImplTest, MultipleProtocolClusterAlpn) {
  AlpnTestConfigFactory alpn_factory;
  Registry::InjectFactory<Server::Configuration::UpstreamTransportSocketConfigFactory>
      registered_factory(alpn_factory);

  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: http12_cluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      typed_extension_protocol_options:
        envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
          "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
          auto_config:
            http2_protocol_options: {}
            http_protocol_options: {}
      transport_socket:
        name: envoy.transport_sockets.alpn
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
  )EOF";
  create(parseBootstrapFromV3Yaml(yaml));
}

TEST_F(ClusterManagerImplTest, MultipleHealthCheckFail) {
  const std::string yaml = R"EOF(
 static_resources:
  clusters:
  - name: service_google
    connect_timeout: 0.25s
    health_checks:
      - timeout: 1s
        interval: 1s
        http_health_check:
          path: "/blah"
      - timeout: 1s
        interval: 1s
        http_health_check:
          path: "/"
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(create(parseBootstrapFromV3Yaml(yaml)), EnvoyException,
                            "Multiple health checks not supported");
}

TEST_F(ClusterManagerImplTest, MultipleProtocolCluster) {
  time_system_.setSystemTime(std::chrono::milliseconds(1234567891234));

  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: http12_cluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      http2_protocol_options: {}
      http_protocol_options: {}
      protocol_selection: USE_DOWNSTREAM_PROTOCOL
  )EOF";
  create(parseBootstrapFromV3Yaml(yaml));
  auto info =
      cluster_manager_->clusters().active_clusters_.find("http12_cluster")->second.get().info();
  EXPECT_NE(0, info->features() & Upstream::ClusterInfo::Features::USE_DOWNSTREAM_PROTOCOL);

  checkConfigDump(R"EOF(
static_clusters:
  - cluster:
      "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
      name: http12_cluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      http2_protocol_options: {}
      http_protocol_options: {}
      protocol_selection: USE_DOWNSTREAM_PROTOCOL
    last_updated:
      seconds: 1234567891
      nanos: 234000000
dynamic_active_clusters:
dynamic_warming_clusters:
)EOF");

  Matchers::MockStringMatcher mock_matcher;
  EXPECT_CALL(mock_matcher, match("http12_cluster")).WillOnce(Return(false));
  checkConfigDump(R"EOF(
static_clusters:
dynamic_active_clusters:
dynamic_warming_clusters:
)EOF",
                  mock_matcher);
}

TEST_F(ClusterManagerImplTest, OutlierEventLog) {
  const std::string json = R"EOF(
  {
    "cluster_manager": {
      "outlier_detection": {
        "event_log_path": "foo"
      }
    },
    "static_resources": {
      "clusters": []
    }
  }
  )EOF";

  EXPECT_CALL(
      factory_.server_context_.access_log_manager_,
      createAccessLog(Filesystem::FilePathAndType{Filesystem::DestinationType::File, "foo"}));
  create(parseBootstrapFromV3Json(json));
}

TEST_F(ClusterManagerImplTest, AdsCluster) {
  // Replace the adsMux to have mocked GrpcMux object that later expectations
  // can be set on.
  std::shared_ptr<NiceMock<Config::MockGrpcMux>> ads_mux =
      std::make_shared<NiceMock<Config::MockGrpcMux>>();
  ON_CALL(factory_.server_context_.xds_manager_, adsMux()).WillByDefault(Return(ads_mux));

  const std::string yaml = R"EOF(
  dynamic_resources:
    ads_config:
      api_type: GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: ads_cluster
  static_resources:
    clusters:
    - name: ads_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: ads_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF";

  EXPECT_CALL(*ads_mux, start());
  create(parseBootstrapFromV3Yaml(yaml));
}

TEST_F(ClusterManagerImplTest, AdsClusterStartsMuxOnlyOnce) {
  // Replace the adsMux to have mocked GrpcMux object that later expectations
  // can be set on.
  std::shared_ptr<NiceMock<Config::MockGrpcMux>> ads_mux =
      std::make_shared<NiceMock<Config::MockGrpcMux>>();
  ON_CALL(factory_.server_context_.xds_manager_, adsMux()).WillByDefault(Return(ads_mux));

  const std::string yaml = R"EOF(
  dynamic_resources:
    ads_config:
      api_type: GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: ads_cluster
  static_resources:
    clusters:
    - name: ads_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: ads_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF";

  EXPECT_CALL(*ads_mux, start());
  create(parseBootstrapFromV3Yaml(yaml));

  const std::string update_static_ads_cluster_yaml = R"EOF(
    name: ads_cluster
    connect_timeout: 0.250s
    type: static
    lb_policy: round_robin
    load_assignment:
      cluster_name: ads_cluster
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 12001
  )EOF";

  // The static ads_cluster should not be updated by the ClusterManager (only dynamic clusters can
  // be added or updated outside of the Bootstrap config).
  EXPECT_FALSE(*cluster_manager_->addOrUpdateCluster(
      parseClusterFromV3Yaml(update_static_ads_cluster_yaml), "version2"));
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(1, factory_.stats_
                   .gauge("cluster_manager.active_clusters", Stats::Gauge::ImportMode::NeverImport)
                   .value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_modified").value());

  const std::string new_cluster_yaml = R"EOF(
    name: new_cluster
    connect_timeout: 0.250s
    type: static
    lb_policy: round_robin
    load_assignment:
      cluster_name: new_cluster
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 12001
  )EOF";

  EXPECT_TRUE(
      *cluster_manager_->addOrUpdateCluster(parseClusterFromV3Yaml(new_cluster_yaml), "version1"));
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(2, factory_.stats_
                   .gauge("cluster_manager.active_clusters", Stats::Gauge::ImportMode::NeverImport)
                   .value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_modified").value());
}

TEST_F(ClusterManagerImplTest, NoSdsConfig) {
  const std::string yaml = R"EOF(
static_resources:
  clusters:
  - name: cluster_1
    connect_timeout: 0.250s
    type: eds
    lb_policy: round_robin
  )EOF";
  EXPECT_THROW_WITH_MESSAGE(create(parseBootstrapFromV3Yaml(yaml)), EnvoyException,
                            "cannot create an EDS cluster without an EDS config");
}

TEST_F(ClusterManagerImplTest, UnknownClusterType) {
  const std::string json = R"EOF(
  {
    "static_resources": {
      "clusters": [
        {
          "name": "cluster_1",
          "connect_timeout": "0.250s",
          "type": "foo",
          "lb_policy": "round_robin"
        }]
      }
    }
  )EOF";

  EXPECT_THROW_WITH_REGEX(create(parseBootstrapFromV3Json(json)), EnvoyException, "foo");
}

TEST_F(ClusterManagerImplTest, LocalClusterNotDefined) {
  const std::string json = fmt::sprintf(
      R"EOF(
  {
    "cluster_manager": {
      "local_cluster_name": "new_cluster",
    },
    "static_resources": {
      %s
    }
  }
  )EOF",
      clustersJson({defaultStaticClusterJson("cluster_1"), defaultStaticClusterJson("cluster_2")}));

  EXPECT_THROW(create(parseBootstrapFromV3Json(json)), EnvoyException);
}

TEST_F(ClusterManagerImplTest, BadClusterManagerConfig) {
  const std::string json = R"EOF(
  {
    "cluster_manager": {
      "outlier_detection": {
        "event_log_path": "foo"
      },
      "fake_property" : "fake_property"
    },
    "static_resources": {
      "clusters": []
    }
  }
  )EOF";

  EXPECT_THROW_WITH_REGEX(create(parseBootstrapFromV3Json(json)), EnvoyException, "fake_property");
}

TEST_F(ClusterManagerImplTest, LocalClusterDefined) {
  const std::string json = fmt::sprintf(
      R"EOF(
  {
    "cluster_manager": {
      "local_cluster_name": "new_cluster",
    },
    "static_resources": {
      %s
    }
  }
  )EOF",
      clustersJson({defaultStaticClusterJson("cluster_1"), defaultStaticClusterJson("cluster_2"),
                    defaultStaticClusterJson("new_cluster")}));

  create(parseBootstrapFromV3Json(json));
  checkStats(3 /*added*/, 0 /*modified*/, 0 /*removed*/, 3 /*active*/, 0 /*warming*/);

  factory_.tls_.shutdownThread();
}

TEST_F(ClusterManagerImplTest, DuplicateCluster) {
  const std::string json = fmt::sprintf(
      "{\"static_resources\":{%s}}",
      clustersJson({defaultStaticClusterJson("cluster_1"), defaultStaticClusterJson("cluster_1")}));
  const auto config = parseBootstrapFromV3Json(json);
  EXPECT_THROW(create(config), EnvoyException);
}

TEST_F(ClusterManagerImplTest, ValidClusterName) {
  const std::string yaml = R"EOF(
static_resources:
  clusters:
  - name: cluster:name
    connect_timeout: 0.250s
    type: static
    lb_policy: round_robin
    load_assignment:
      cluster_name: foo
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 11001
  )EOF";

  create(parseBootstrapFromV3Yaml(yaml));
  cluster_manager_->clusters()
      .active_clusters_.find("cluster:name")
      ->second.get()
      .info()
      ->statsScope()
      .counterFromString("foo")
      .inc();
  EXPECT_EQ(1UL, factory_.stats_.counter("cluster.cluster_name.foo").value());
}

// Validate that the primary clusters are derived from the bootstrap and don't
// include EDS.
TEST_F(ClusterManagerImplTest, PrimaryClusters) {
  const std::string yaml = R"EOF(
static_resources:
  clusters:
  - name: static_cluster
    connect_timeout: 0.250s
    type: static
  - name: logical_dns_cluster
    connect_timeout: 0.250s
    type: logical_dns
    load_assignment:
      endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: foo.com
                  port_value: 11001
  - name: strict_dns_cluster
    connect_timeout: 0.250s
    type: strict_dns
    load_assignment:
      endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: foo.com
                  port_value: 11001
  - name: rest_eds_cluster
    connect_timeout: 0.250s
    type: eds
    eds_cluster_config:
      eds_config:
        api_config_source:
          api_type: GRPC
          grpc_services:
            envoy_grpc:
              cluster_name: static_cluster
  )EOF";
  create(parseBootstrapFromV3Yaml(yaml));
  const auto& primary_clusters = cluster_manager_->primaryClusters();
  EXPECT_THAT(primary_clusters, testing::UnorderedElementsAre(
                                    "static_cluster", "strict_dns_cluster", "logical_dns_cluster"));
}

TEST_F(ClusterManagerImplTest, OriginalDstLbRestriction) {
  const std::string yaml = R"EOF(
static_resources:
  clusters:
  - name: cluster_1
    connect_timeout: 0.250s
    type: original_dst
    lb_policy: round_robin
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      create(parseBootstrapFromV3Yaml(yaml)), EnvoyException,
      "cluster: LB policy ROUND_ROBIN is not valid for Cluster type ORIGINAL_DST. Only "
      "'CLUSTER_PROVIDED' is allowed with cluster type 'ORIGINAL_DST'");
}

TEST_F(ClusterManagerImplTest, SubsetLoadBalancerClusterProvidedLbRestriction) {
  const std::string yaml = R"EOF(
 static_resources:
  clusters:
  - name: cluster_1
    connect_timeout: 0.250s
    type: static
    lb_policy: cluster_provided
    lb_subset_config:
      fallback_policy: ANY_ENDPOINT
      subset_selectors:
        - keys: [ "x" ]
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      create(parseBootstrapFromV3Yaml(yaml)), EnvoyException,
      "cluster: LB policy CLUSTER_PROVIDED cannot be combined with lb_subset_config");
}

TEST_F(ClusterManagerImplTest, SubsetLoadBalancerLocalityAware) {
  const std::string yaml = R"EOF(
 static_resources:
  clusters:
  - name: cluster_1
    connect_timeout: 0.250s
    type: STATIC
    lb_policy: ROUND_ROBIN
    lb_subset_config:
      fallback_policy: ANY_ENDPOINT
      subset_selectors:
        - keys: [ "x" ]
      locality_weight_aware: true
    load_assignment:
      cluster_name: cluster_1
      endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 8000
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 8001
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(create(parseBootstrapFromV3Yaml(yaml)), EnvoyException,
                            "Locality weight aware subset LB requires that a "
                            "locality_weighted_lb_config be set in cluster_1");
}

TEST_F(ClusterManagerImplTest, RingHashLoadBalancerInitialization) {
  const std::string yaml = R"EOF(
 static_resources:
  clusters:
  - name: redis_cluster
    lb_policy: RING_HASH
    ring_hash_lb_config:
      minimum_ring_size: 125
    connect_timeout: 0.250s
    type: STATIC
    load_assignment:
      cluster_name: redis_cluster
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 8000
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 8001
  )EOF";
  create(parseBootstrapFromV3Yaml(yaml));
}

TEST_F(ClusterManagerImplTest, RingHashLoadBalancerV2Initialization) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: redis_cluster
      connect_timeout: 0.250s
      lb_policy: RING_HASH
      load_assignment:
        cluster_name: redis_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 8000
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 8001
      dns_lookup_family: V4_ONLY
      ring_hash_lb_config:
        minimum_ring_size: 125
  )EOF";
  create(parseBootstrapFromV3Yaml(yaml));
}

// Verify EDS clusters have EDS config.
TEST_F(ClusterManagerImplTest, EdsClustersRequireEdsConfig) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_0
      type: EDS
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(create(parseBootstrapFromV3Yaml(yaml)), EnvoyException,
                            "cannot create an EDS cluster without an EDS config");
}

// Verify that specifying a cluster provided LB, but the cluster doesn't provide one is an error.
TEST_F(ClusterManagerImplTest, ClusterProvidedLbNoLb) {
  const std::string json = fmt::sprintf("{\"static_resources\":{%s}}",
                                        clustersJson({defaultStaticClusterJson("cluster_0")}));

  std::shared_ptr<MockClusterMockPrioritySet> cluster1(new NiceMock<MockClusterMockPrioritySet>());
  cluster1->info_->name_ = "cluster_0";

  ON_CALL(*cluster1->info_, loadBalancerFactory())
      .WillByDefault(
          ReturnRef(Config::Utility::getAndCheckFactoryByName<Upstream::TypedLoadBalancerFactory>(
              "envoy.load_balancing_policies.cluster_provided")));

  EXPECT_CALL(factory_, clusterFromProto_(_, _, _))
      .WillOnce(Return(std::make_pair(cluster1, nullptr)));
  EXPECT_THROW_WITH_MESSAGE(create(parseBootstrapFromV3Json(json)), EnvoyException,
                            "cluster manager: cluster provided LB specified but cluster "
                            "'cluster_0' did not provide one. Check cluster documentation.");
}

// Verify that not specifying a cluster provided LB, but the cluster does provide one is an error.
TEST_F(ClusterManagerImplTest, ClusterProvidedLbNotConfigured) {
  const std::string json = fmt::sprintf("{\"static_resources\":{%s}}",
                                        clustersJson({defaultStaticClusterJson("cluster_0")}));

  std::shared_ptr<MockClusterMockPrioritySet> cluster1(new NiceMock<MockClusterMockPrioritySet>());
  cluster1->info_->name_ = "cluster_0";
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _))
      .WillOnce(Return(std::make_pair(cluster1, new MockThreadAwareLoadBalancer())));
  EXPECT_THROW_WITH_MESSAGE(create(parseBootstrapFromV3Json(json)), EnvoyException,
                            "cluster manager: cluster provided LB not specified but cluster "
                            "'cluster_0' provided one. Check cluster documentation.");
}

// Verify that multiple load balancing policies can be specified, and Envoy selects the first
// policy that it has a factory for.
TEST_F(ClusterManagerImplTest, LbPolicyConfig) {
  // envoy.load_balancers.custom_lb is registered by linking in
  // //test/integration/load_balancers:custom_lb_policy.
  const std::string yaml = fmt::format(R"EOF(
 static_resources:
  clusters:
  - name: cluster_1
    connect_timeout: 0.250s
    type: STATIC
    lb_policy: LOAD_BALANCING_POLICY_CONFIG
    load_balancing_policy:
      policies:
      - typed_extension_config:
          name: envoy.load_balancers.unknown_lb
      - typed_extension_config:
          name: envoy.load_balancers.custom_lb
          typed_config:
            "@type": type.googleapis.com/test.integration.custom_lb.CustomLbConfig
    load_assignment:
      cluster_name: cluster_1
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 8000
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 8001
  )EOF");

  create(parseBootstrapFromV3Yaml(yaml));
  const auto& cluster = cluster_manager_->clusters().getCluster("cluster_1");
  EXPECT_NE(cluster, absl::nullopt);
  EXPECT_TRUE(cluster->get().info()->loadBalancerConfig().has_value());
}

TEST_F(ClusterManagerImplTest, TcpHealthChecker) {
  const std::string yaml = R"EOF(
 static_resources:
  clusters:
  - name: cluster_1
    connect_timeout: 0.250s
    type: STATIC
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: cluster_1
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 11001
    health_checks:
    - timeout: 1s
      interval: 1s
      unhealthy_threshold: 2
      healthy_threshold: 2
      tcp_health_check:
        send:
          text: '01'
        receive:
          - text: '02'
  )EOF";

  Network::MockClientConnection* connection = new NiceMock<Network::MockClientConnection>();
  EXPECT_CALL(factory_.dispatcher_,
              createClientConnection_(
                  PointeesEq(*Network::Utility::resolveUrl("tcp://127.0.0.1:11001")), _, _, _))
      .WillOnce(Return(connection));
  create(parseBootstrapFromV3Yaml(yaml));
  factory_.tls_.shutdownThread();
}

TEST_F(ClusterManagerImplTest, HttpHealthChecker) {
  const std::string yaml = R"EOF(
 static_resources:
  clusters:
  - name: cluster_1
    connect_timeout: 0.250s
    type: STATIC
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: cluster_1
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 11001
    health_checks:
    - timeout: 1s
      interval: 1s
      unhealthy_threshold: 2
      healthy_threshold: 2
      http_health_check:
        path: "/healthcheck"
  )EOF";

  Network::MockClientConnection* connection = new NiceMock<Network::MockClientConnection>();
  EXPECT_CALL(factory_.dispatcher_,
              createClientConnection_(
                  PointeesEq(*Network::Utility::resolveUrl("tcp://127.0.0.1:11001")), _, _, _))
      .WillOnce(Return(connection));
  EXPECT_CALL(factory_.dispatcher_, deleteInDispatcherThread(_));
  create(parseBootstrapFromV3Yaml(yaml));
  factory_.tls_.shutdownThread();
  factory_.dispatcher_.to_delete_.clear();
}

TEST_F(ClusterManagerImplTest, UnknownCluster) {
  const std::string json = fmt::sprintf("{\"static_resources\":{%s}}",
                                        clustersJson({defaultStaticClusterJson("cluster_1")}));

  create(parseBootstrapFromV3Json(json));
  EXPECT_EQ(nullptr, cluster_manager_->getThreadLocalCluster("hello"));
  factory_.tls_.shutdownThread();
}

/**
 * Test that buffer limits are set on new TCP connections.
 */
TEST_F(ClusterManagerImplTest, VerifyBufferLimits) {
  const std::string yaml = R"EOF(
 static_resources:
  clusters:
  - name: cluster_1
    connect_timeout: 0.250s
    type: static
    lb_policy: round_robin
    per_connection_buffer_limit_bytes: 8192
    load_assignment:
      cluster_name: cluster_1
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 11001
  )EOF";

  create(parseBootstrapFromV3Yaml(yaml));
  Network::MockClientConnection* connection = new NiceMock<Network::MockClientConnection>();
  EXPECT_CALL(*connection, setBufferLimits(8192));
  EXPECT_CALL(factory_.tls_.dispatcher_, createClientConnection_(_, _, _, _))
      .WillOnce(Return(connection));
  auto conn_data = cluster_manager_->getThreadLocalCluster("cluster_1")->tcpConn(nullptr);
  EXPECT_EQ(connection, conn_data.connection_.get());
  factory_.tls_.shutdownThread();
}

TEST_F(ClusterManagerImplTest, TwoEqualCommonLbConfigSharedPool) {
  create(defaultConfig());

  envoy::config::cluster::v3::Cluster::CommonLbConfig config_a;
  envoy::config::cluster::v3::Cluster::CommonLbConfig config_b;

  config_a.mutable_healthy_panic_threshold()->set_value(0.3);
  config_b.mutable_healthy_panic_threshold()->set_value(0.3);
  auto common_config_ptr_a = cluster_manager_->getCommonLbConfigPtr(config_a);
  auto common_config_ptr_b = cluster_manager_->getCommonLbConfigPtr(config_b);
  EXPECT_EQ(common_config_ptr_a, common_config_ptr_b);
}

TEST_F(ClusterManagerImplTest, TwoUnequalCommonLbConfigSharedPool) {
  create(defaultConfig());

  envoy::config::cluster::v3::Cluster::CommonLbConfig config_a;
  envoy::config::cluster::v3::Cluster::CommonLbConfig config_b;

  config_a.mutable_healthy_panic_threshold()->set_value(0.3);
  config_b.mutable_healthy_panic_threshold()->set_value(0.5);
  auto common_config_ptr_a = cluster_manager_->getCommonLbConfigPtr(config_a);
  auto common_config_ptr_b = cluster_manager_->getCommonLbConfigPtr(config_b);
  EXPECT_NE(common_config_ptr_a, common_config_ptr_b);
}

// Test that custom DNS resolver is used, when custom resolver is configured per cluster.
TEST_F(ClusterManagerImplTest, CustomDnsResolverSpecified) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.250s
      type: STRICT_DNS
      dns_resolution_config:
        resolvers:
          - socket_address:
              address: 1.2.3.4
              port_value: 80
  )EOF";

  std::shared_ptr<Network::MockDnsResolver> dns_resolver(new Network::MockDnsResolver());
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  envoy::config::core::v3::Address resolvers;
  Network::Utility::addressToProtobufAddress(Network::Address::Ipv4Instance("1.2.3.4", 80),
                                             resolvers);
  cares.add_resolvers()->MergeFrom(resolvers);
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  typed_dns_resolver_config.mutable_typed_config()->PackFrom(cares);
  typed_dns_resolver_config.set_name(std::string(Network::CaresDnsResolver));

  // As custom resolver is specified via field `dns_resolution_config.resolvers` in clusters
  // config, the method `createDnsResolver` is called once.
  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, ProtoEq(typed_dns_resolver_config)))
      .WillOnce(Return(dns_resolver));

  Network::DnsResolver::ResolveCb dns_callback;
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));
  factory_.tls_.shutdownThread();
}

// Test that custom DNS resolver is used, when custom resolver is configured per cluster,
// and multiple resolvers are configured.
TEST_F(ClusterManagerImplTest, CustomDnsResolverSpecifiedMultipleResolvers) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.250s
      type: STRICT_DNS
      dns_resolution_config:
        resolvers:
          - socket_address:
              address: 1.2.3.4
              port_value: 80
          - socket_address:
              address: 1.2.3.5
              port_value: 81
  )EOF";

  std::shared_ptr<Network::MockDnsResolver> dns_resolver(new Network::MockDnsResolver());
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  envoy::config::core::v3::Address resolvers;
  Network::Utility::addressToProtobufAddress(Network::Address::Ipv4Instance("1.2.3.4", 80),
                                             resolvers);
  cares.add_resolvers()->MergeFrom(resolvers);
  Network::Utility::addressToProtobufAddress(Network::Address::Ipv4Instance("1.2.3.5", 81),
                                             resolvers);
  cares.add_resolvers()->MergeFrom(resolvers);
  typed_dns_resolver_config.mutable_typed_config()->PackFrom(cares);
  typed_dns_resolver_config.set_name(std::string(Network::CaresDnsResolver));

  // As custom resolver is specified via field `dns_resolution_config.resolvers` in clusters
  // config, the method `createDnsResolver` is called once.
  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, ProtoEq(typed_dns_resolver_config)))
      .WillOnce(Return(dns_resolver));

  Network::DnsResolver::ResolveCb dns_callback;
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));
  factory_.tls_.shutdownThread();
}

// Test that custom DNS resolver is used and overriding the specified deprecated field
// `dns_resolvers`.
TEST_F(ClusterManagerImplTest, CustomDnsResolverSpecifiedOveridingDeprecatedResolver) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.250s
      type: STRICT_DNS
      dns_resolvers:
      - socket_address:
          address: 1.2.3.4
          port_value: 80
      dns_resolution_config:
        resolvers:
          - socket_address:
              address: 1.2.3.5
              port_value: 81
  )EOF";

  std::shared_ptr<Network::MockDnsResolver> dns_resolver(new Network::MockDnsResolver());
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  envoy::config::core::v3::Address resolvers;
  Network::Utility::addressToProtobufAddress(Network::Address::Ipv4Instance("1.2.3.5", 81),
                                             resolvers);
  cares.add_resolvers()->MergeFrom(resolvers);
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  typed_dns_resolver_config.mutable_typed_config()->PackFrom(cares);
  typed_dns_resolver_config.set_name(std::string(Network::CaresDnsResolver));

  // As custom resolver is specified via field `dns_resolution_config.resolvers` in clusters
  // config, the method `createDnsResolver` is called once.
  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, ProtoEq(typed_dns_resolver_config)))
      .WillOnce(Return(dns_resolver));

  Network::DnsResolver::ResolveCb dns_callback;
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));
  factory_.tls_.shutdownThread();
}

// Test that custom DNS resolver with UDP lookups is used, when custom resolver is configured
// per cluster and `use_tcp_for_dns_lookups` is not specified.
TEST_F(ClusterManagerImplTest, UseUdpWithCustomDnsResolver) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.250s
      type: STRICT_DNS
      dns_resolution_config:
        resolvers:
          - socket_address:
              address: 1.2.3.4
              port_value: 80
  )EOF";

  std::shared_ptr<Network::MockDnsResolver> dns_resolver(new Network::MockDnsResolver());
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, _))
      .WillOnce(DoAll(SaveArg<2>(&typed_dns_resolver_config), Return(dns_resolver)));

  Network::DnsResolver::ResolveCb dns_callback;
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));

  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  EXPECT_EQ(false, cares.dns_resolver_options().use_tcp_for_dns_lookups());
  factory_.tls_.shutdownThread();
}

// Test that custom DNS resolver with TCP lookups is used, when custom resolver is configured
// per cluster and `use_tcp_for_dns_lookups` (deprecated field) is specified as true.
TEST_F(ClusterManagerImplTest, UseTcpWithCustomDnsResolverViaDeprecatedField) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      use_tcp_for_dns_lookups: true
      connect_timeout: 0.250s
      type: STRICT_DNS
      dns_resolvers:
      - socket_address:
          address: 1.2.3.4
          port_value: 80
  )EOF";

  std::shared_ptr<Network::MockDnsResolver> dns_resolver(new Network::MockDnsResolver());
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, _))
      .WillOnce(DoAll(SaveArg<2>(&typed_dns_resolver_config), Return(dns_resolver)));

  Network::DnsResolver::ResolveCb dns_callback;
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));

  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  EXPECT_EQ(true, cares.dns_resolver_options().use_tcp_for_dns_lookups());
  factory_.tls_.shutdownThread();
}

// Test that custom DNS resolver with UDP lookups is used, when custom resolver is configured
// per cluster and `use_tcp_for_dns_lookups` is specified as true but is overridden
// by dns_resolution_config.dns_resolver_options.use_tcp_for_dns_lookups which is set as false.
TEST_F(ClusterManagerImplTest, UseUdpWithCustomDnsResolverDeprecatedFieldOverridden) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      use_tcp_for_dns_lookups: true
      connect_timeout: 0.250s
      type: STRICT_DNS
      dns_resolution_config:
        dns_resolver_options:
          use_tcp_for_dns_lookups: false
        resolvers:
          - socket_address:
              address: 1.2.3.4
              port_value: 80
  )EOF";

  std::shared_ptr<Network::MockDnsResolver> dns_resolver(new Network::MockDnsResolver());
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, _))
      .WillOnce(DoAll(SaveArg<2>(&typed_dns_resolver_config), Return(dns_resolver)));

  Network::DnsResolver::ResolveCb dns_callback;
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  EXPECT_EQ(false, cares.dns_resolver_options().use_tcp_for_dns_lookups());
  factory_.tls_.shutdownThread();
}

// Test that custom DNS resolver with TCP lookups is used, when custom resolver is configured
// per cluster and `use_tcp_for_dns_lookups` is specified as false but is overridden
// by dns_resolution_config.dns_resolver_options.use_tcp_for_dns_lookups which is specified as true.
TEST_F(ClusterManagerImplTest, UseTcpWithCustomDnsResolverDeprecatedFieldOverridden) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      use_tcp_for_dns_lookups: false
      connect_timeout: 0.250s
      type: STRICT_DNS
      dns_resolution_config:
        dns_resolver_options:
          use_tcp_for_dns_lookups: true
        resolvers:
          - socket_address:
              address: 1.2.3.4
              port_value: 80
  )EOF";

  std::shared_ptr<Network::MockDnsResolver> dns_resolver(new Network::MockDnsResolver());
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, _))
      .WillOnce(DoAll(SaveArg<2>(&typed_dns_resolver_config), Return(dns_resolver)));

  Network::DnsResolver::ResolveCb dns_callback;
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));

  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  EXPECT_EQ(true, cares.dns_resolver_options().use_tcp_for_dns_lookups());
  factory_.tls_.shutdownThread();
}

// Test that custom DNS resolver with TCP lookups is used, when custom resolver is configured
// per cluster and `dns_resolution_config.dns_resolver_options.use_tcp_for_dns_lookups` is enabled
// for that cluster.
TEST_F(ClusterManagerImplTest, UseTcpWithCustomDnsResolver) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.250s
      type: STRICT_DNS
      dns_resolution_config:
        dns_resolver_options:
          use_tcp_for_dns_lookups: true
        resolvers:
          - socket_address:
              address: 1.2.3.4
              port_value: 80
  )EOF";

  std::shared_ptr<Network::MockDnsResolver> dns_resolver(new Network::MockDnsResolver());
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, _))
      .WillOnce(DoAll(SaveArg<2>(&typed_dns_resolver_config), Return(dns_resolver)));

  Network::DnsResolver::ResolveCb dns_callback;
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));

  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  EXPECT_EQ(true, cares.dns_resolver_options().use_tcp_for_dns_lookups());
  factory_.tls_.shutdownThread();
}

// Test that custom DNS resolver with default search domain, when custom resolver is configured
// per cluster and `no_default_search_domain` is not specified.
TEST_F(ClusterManagerImplTest, DefaultSearchDomainWithCustomDnsResolver) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.250s
      type: STRICT_DNS
      dns_resolution_config:
        resolvers:
          - socket_address:
              address: 1.2.3.4
              port_value: 80
  )EOF";

  std::shared_ptr<Network::MockDnsResolver> dns_resolver(new Network::MockDnsResolver());
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, _))
      .WillOnce(DoAll(SaveArg<2>(&typed_dns_resolver_config), Return(dns_resolver)));

  Network::DnsResolver::ResolveCb dns_callback;
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));

  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  EXPECT_EQ(false, cares.dns_resolver_options().no_default_search_domain());
  factory_.tls_.shutdownThread();
}

// Test that custom DNS resolver with default search domain, when custom resolver is configured
// per cluster and `no_default_search_domain` is specified as false.
TEST_F(ClusterManagerImplTest, DefaultSearchDomainWithCustomDnsResolverWithConfig) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.250s
      type: STRICT_DNS
      dns_resolution_config:
        dns_resolver_options:
          no_default_search_domain: false
        resolvers:
          - socket_address:
              address: 1.2.3.4
              port_value: 80
  )EOF";

  std::shared_ptr<Network::MockDnsResolver> dns_resolver(new Network::MockDnsResolver());
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, _))
      .WillOnce(DoAll(SaveArg<2>(&typed_dns_resolver_config), Return(dns_resolver)));

  Network::DnsResolver::ResolveCb dns_callback;
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));

  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  EXPECT_EQ(false, cares.dns_resolver_options().no_default_search_domain());
  factory_.tls_.shutdownThread();
}

// Test that custom DNS resolver with no default search domain, when custom resolver is
// configured per cluster and `no_default_search_domain` is specified as true.
TEST_F(ClusterManagerImplTest, NoDefaultSearchDomainWithCustomDnsResolver) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.250s
      type: STRICT_DNS
      dns_resolution_config:
        dns_resolver_options:
          no_default_search_domain: true
        resolvers:
          - socket_address:
              address: 1.2.3.4
              port_value: 80
  )EOF";

  std::shared_ptr<Network::MockDnsResolver> dns_resolver(new Network::MockDnsResolver());
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, _))
      .WillOnce(DoAll(SaveArg<2>(&typed_dns_resolver_config), Return(dns_resolver)));

  Network::DnsResolver::ResolveCb dns_callback;
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));

  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  EXPECT_EQ(true, cares.dns_resolver_options().no_default_search_domain());
  factory_.tls_.shutdownThread();
}

// Test that typed_dns_resolver_config is specified and is used.
TEST_F(ClusterManagerImplTest, TypedDnsResolverConfigSpecified) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.250s
      type: STRICT_DNS
      typed_dns_resolver_config:
        name: envoy.network.dns_resolver.cares
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig
          resolvers:
          - socket_address:
              address: "1.2.3.4"
              port_value: 80
          dns_resolver_options:
            use_tcp_for_dns_lookups: true
            no_default_search_domain: true
  )EOF";

  std::shared_ptr<Network::MockDnsResolver> dns_resolver(new Network::MockDnsResolver());
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, _))
      .WillOnce(DoAll(SaveArg<2>(&typed_dns_resolver_config), Return(dns_resolver)));

  Network::DnsResolver::ResolveCb dns_callback;
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));

  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  envoy::config::core::v3::Address resolvers;
  Network::Utility::addressToProtobufAddress(Network::Address::Ipv4Instance("1.2.3.4", 80),
                                             resolvers);
  EXPECT_EQ(true, cares.dns_resolver_options().use_tcp_for_dns_lookups());
  EXPECT_EQ(true, cares.dns_resolver_options().no_default_search_domain());
  EXPECT_EQ(true, TestUtility::protoEqual(cares.resolvers(0), resolvers));
  factory_.tls_.shutdownThread();
}

// Test that resolvers in typed_dns_resolver_config is specified and is used.
TEST_F(ClusterManagerImplTest, TypedDnsResolverConfigResolversSpecified) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.250s
      type: STRICT_DNS
      typed_dns_resolver_config:
        name: envoy.network.dns_resolver.cares
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig
          resolvers:
          - socket_address:
              address: "1.2.3.4"
              port_value: 80
  )EOF";

  std::shared_ptr<Network::MockDnsResolver> dns_resolver(new Network::MockDnsResolver());
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, _))
      .WillOnce(DoAll(SaveArg<2>(&typed_dns_resolver_config), Return(dns_resolver)));

  Network::DnsResolver::ResolveCb dns_callback;
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));

  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  envoy::config::core::v3::Address resolvers;
  Network::Utility::addressToProtobufAddress(Network::Address::Ipv4Instance("1.2.3.4", 80),
                                             resolvers);
  EXPECT_EQ(false, cares.dns_resolver_options().use_tcp_for_dns_lookups());
  EXPECT_EQ(false, cares.dns_resolver_options().no_default_search_domain());
  EXPECT_EQ(true, TestUtility::protoEqual(cares.resolvers(0), resolvers));
  factory_.tls_.shutdownThread();
}

// Test that multiple resolvers in typed_dns_resolver_config is specified and is used.
TEST_F(ClusterManagerImplTest, TypedDnsResolverConfigMultipleResolversSpecified) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.250s
      type: STRICT_DNS
      typed_dns_resolver_config:
        name: envoy.network.dns_resolver.cares
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig
          resolvers:
          - socket_address:
              address: "1.2.3.4"
              port_value: 80
          - socket_address:
              address: "1.2.3.5"
              port_value: 81
  )EOF";

  std::shared_ptr<Network::MockDnsResolver> dns_resolver(new Network::MockDnsResolver());
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, _))
      .WillOnce(DoAll(SaveArg<2>(&typed_dns_resolver_config), Return(dns_resolver)));

  Network::DnsResolver::ResolveCb dns_callback;
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));

  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  envoy::config::core::v3::Address resolvers;
  Network::Utility::addressToProtobufAddress(Network::Address::Ipv4Instance("1.2.3.4", 80),
                                             resolvers);
  EXPECT_EQ(false, cares.dns_resolver_options().use_tcp_for_dns_lookups());
  EXPECT_EQ(false, cares.dns_resolver_options().no_default_search_domain());
  EXPECT_EQ(true, TestUtility::protoEqual(cares.resolvers(0), resolvers));
  Network::Utility::addressToProtobufAddress(Network::Address::Ipv4Instance("1.2.3.5", 81),
                                             resolvers);
  EXPECT_EQ(true, TestUtility::protoEqual(cares.resolvers(1), resolvers));
  factory_.tls_.shutdownThread();
}

// Test that dns_resolver_options in typed_dns_resolver_config is specified and is used.
TEST_F(ClusterManagerImplTest, TypedDnsResolverConfigResolverOptionsSpecified) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.250s
      type: STRICT_DNS
      typed_dns_resolver_config:
        name: envoy.network.dns_resolver.cares
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig
          dns_resolver_options:
            use_tcp_for_dns_lookups: true
            no_default_search_domain: true
  )EOF";

  std::shared_ptr<Network::MockDnsResolver> dns_resolver(new Network::MockDnsResolver());
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, _))
      .WillOnce(DoAll(SaveArg<2>(&typed_dns_resolver_config), Return(dns_resolver)));

  Network::DnsResolver::ResolveCb dns_callback;
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));

  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  EXPECT_EQ(true, cares.dns_resolver_options().use_tcp_for_dns_lookups());
  EXPECT_EQ(true, cares.dns_resolver_options().no_default_search_domain());
  EXPECT_EQ(0, cares.resolvers().size());
  factory_.tls_.shutdownThread();
}

// Test that when typed_dns_resolver_config is specified, it is used. All other deprecated
// configurations are ignored, which includes dns_resolvers, use_tcp_for_dns_lookups, and
// dns_resolution_config.
TEST_F(ClusterManagerImplTest, TypedDnsResolverConfigSpecifiedOveridingDeprecatedConfig) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.250s
      type: STRICT_DNS
      dns_resolvers:
      - socket_address:
          address: 1.2.3.4
          port_value: 80
      use_tcp_for_dns_lookups: false
      dns_resolution_config:
        resolvers:
          - socket_address:
              address: 1.2.3.5
              port_value: 81
        dns_resolver_options:
          use_tcp_for_dns_lookups: false
          no_default_search_domain: false
      typed_dns_resolver_config:
        name: envoy.network.dns_resolver.cares
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig
          resolvers:
          - socket_address:
              address: "9.10.11.12"
              port_value: 100
          - socket_address:
              address: "5.6.7.8"
              port_value: 200
          dns_resolver_options:
            use_tcp_for_dns_lookups: true
            no_default_search_domain: true
  )EOF";

  std::shared_ptr<Network::MockDnsResolver> dns_resolver(new Network::MockDnsResolver());
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, _))
      .WillOnce(DoAll(SaveArg<2>(&typed_dns_resolver_config), Return(dns_resolver)));

  Network::DnsResolver::ResolveCb dns_callback;
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));

  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  envoy::config::core::v3::Address resolvers;
  Network::Utility::addressToProtobufAddress(Network::Address::Ipv4Instance("9.10.11.12", 100),
                                             resolvers);
  EXPECT_EQ(true, cares.dns_resolver_options().use_tcp_for_dns_lookups());
  EXPECT_EQ(true, cares.dns_resolver_options().no_default_search_domain());
  EXPECT_EQ(true, TestUtility::protoEqual(cares.resolvers(0), resolvers));
  Network::Utility::addressToProtobufAddress(Network::Address::Ipv4Instance("5.6.7.8", 200),
                                             resolvers);
  EXPECT_EQ(true, TestUtility::protoEqual(cares.resolvers(1), resolvers));
  factory_.tls_.shutdownThread();
}

TEST_F(ClusterManagerImplTest, OriginalDstInitialization) {
  const std::string yaml = R"EOF(
  {
    "static_resources": {
      "clusters": [
        {
          "name": "cluster_1",
          "connect_timeout": "0.250s",
          "type": "original_dst",
          "lb_policy": "cluster_provided"
        }
      ]
    }
  }
  )EOF";

  ReadyWatcher initialized;
  EXPECT_CALL(initialized, ready());

  create(parseBootstrapFromV3Yaml(yaml));

  // Set up for an initialize callback.
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });
  const auto all_clusters = cluster_manager_->clusters();
  EXPECT_TRUE(all_clusters.warming_clusters_.empty());
  EXPECT_EQ(all_clusters.active_clusters_.size(), 1);
  EXPECT_FALSE(all_clusters.active_clusters_.at("cluster_1").get().info()->addedViaApi());

  // Test for no hosts returning the correct values before we have hosts.
  EXPECT_EQ(absl::nullopt,
            cluster_manager_->getThreadLocalCluster("cluster_1")
                ->httpConnPool(
                    cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
                    ResourcePriority::Default, Http::Protocol::Http11, nullptr));
  EXPECT_EQ(absl::nullopt,
            cluster_manager_->getThreadLocalCluster("cluster_1")
                ->tcpConnPool(
                    cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
                    ResourcePriority::Default, nullptr));
  EXPECT_EQ(nullptr,
            cluster_manager_->getThreadLocalCluster("cluster_1")->tcpConn(nullptr).connection_);
  EXPECT_EQ(3UL, factory_.stats_.counter("cluster.cluster_1.upstream_cx_none_healthy").value());

  factory_.tls_.shutdownThread();
}

TEST_F(ClusterManagerImplTest, GetActiveOrWarmingCluster) {
  // Start with a static cluster.
  const std::string bootstrap_yaml = R"EOF(
  static_resources:
    clusters:
    - name: static_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: static_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF";
  create(parseBootstrapFromV3Yaml(bootstrap_yaml));

  // Static cluster should be active.
  EXPECT_NE(absl::nullopt, cluster_manager_->getActiveCluster("static_cluster"));
  EXPECT_NE(absl::nullopt, cluster_manager_->getActiveOrWarmingCluster("static_cluster"));
  EXPECT_EQ(absl::nullopt, cluster_manager_->getActiveOrWarmingCluster("non_existent_cluster"));

  // Now, add a dynamic cluster. It will start in warming state.
  const std::string warming_cluster_yaml = R"EOF(
    name: warming_cluster
    connect_timeout: 0.250s
    type: EDS
    eds_cluster_config:
      eds_config:
        api_config_source:
          api_type: GRPC
          grpc_services:
            envoy_grpc:
              cluster_name: static_cluster
  )EOF";
  auto warming_cluster_config = parseClusterFromV3Yaml(warming_cluster_yaml);

  // Mock the cluster creation for the warming cluster.
  std::shared_ptr<MockClusterMockPrioritySet> warming_cluster =
      std::make_shared<NiceMock<MockClusterMockPrioritySet>>();
  warming_cluster->info_->name_ = "warming_cluster";
  std::function<void()> cluster_init_callback;
  EXPECT_CALL(*warming_cluster, initialize(_)).WillOnce(SaveArg<0>(&cluster_init_callback));
  EXPECT_CALL(factory_, clusterFromProto_(ProtoEq(warming_cluster_config), _, true))
      .WillOnce(Return(std::make_pair(warming_cluster, nullptr)));

  // Add the cluster.
  EXPECT_TRUE(*cluster_manager_->addOrUpdateCluster(warming_cluster_config, "version1"));

  // The cluster should be in warming, not active.
  EXPECT_EQ(absl::nullopt, cluster_manager_->getActiveCluster("warming_cluster"));
  OptRef<const Cluster> cluster = cluster_manager_->getActiveOrWarmingCluster("warming_cluster");
  EXPECT_NE(absl::nullopt, cluster);
  EXPECT_EQ("warming_cluster", cluster->info()->name());

  // Finish initialization. This should move it to active.
  cluster_init_callback();

  // Now the cluster should be active.
  cluster = cluster_manager_->getActiveCluster("warming_cluster");
  EXPECT_NE(absl::nullopt, cluster);
  EXPECT_EQ("warming_cluster", cluster->info()->name());
  cluster = cluster_manager_->getActiveOrWarmingCluster("warming_cluster");
  EXPECT_NE(absl::nullopt, cluster);
  EXPECT_EQ("warming_cluster", cluster->info()->name());
}

TEST_F(ClusterManagerImplTest, UpstreamSocketOptionsPassedToTcpConnPool) {
  createWithBasicStaticCluster();
  NiceMock<MockLoadBalancerContext> context;

  auto to_create = new Tcp::ConnectionPool::MockInstance();
  Network::Socket::OptionsSharedPtr options_to_return =
      Network::SocketOptionFactory::buildIpTransparentOptions();

  EXPECT_CALL(context, upstreamSocketOptions()).WillOnce(Return(options_to_return));
  EXPECT_CALL(factory_, allocateTcpConnPool_(_)).WillOnce(Return(to_create));
  EXPECT_CALL(*to_create, addIdleCallback(_));

  auto opt_cp = cluster_manager_->getThreadLocalCluster("cluster_1")
                    ->tcpConnPool(ResourcePriority::Default, &context);
  EXPECT_TRUE(opt_cp.has_value());
}

TEST_F(ClusterManagerImplTest, SelectOverrideHostTestNoOverrideHost) {
  createWithBasicStaticCluster();
  NiceMock<MockLoadBalancerContext> context;

  auto to_create = new Tcp::ConnectionPool::MockInstance();

  EXPECT_CALL(context, overrideHostToSelect()).WillOnce(Return(absl::nullopt));

  EXPECT_CALL(factory_, allocateTcpConnPool_(_)).WillOnce(Return(to_create));
  EXPECT_CALL(*to_create, addIdleCallback(_));

  auto opt_cp = cluster_manager_->getThreadLocalCluster("cluster_1")
                    ->tcpConnPool(ResourcePriority::Default, &context);
  EXPECT_TRUE(opt_cp.has_value());
}

TEST_F(ClusterManagerImplTest, SelectOverrideHostTestWithOverrideHost) {
  createWithBasicStaticCluster();
  NiceMock<MockLoadBalancerContext> context;

  auto to_create = new Tcp::ConnectionPool::MockInstance();

  EXPECT_CALL(context, overrideHostToSelect())
      .WillRepeatedly(Return(absl::make_optional<Upstream::LoadBalancerContext::OverrideHost>(
          std::make_pair("127.0.0.1:11001", false))));

  EXPECT_CALL(factory_, allocateTcpConnPool_(_))
      .WillOnce(testing::Invoke([&](HostConstSharedPtr host) {
        EXPECT_EQ("127.0.0.1:11001", host->address()->asStringView());
        return to_create;
      }));
  EXPECT_CALL(*to_create, addIdleCallback(_));

  auto opt_cp_1 = cluster_manager_->getThreadLocalCluster("cluster_1")
                      ->tcpConnPool(ResourcePriority::Default, &context);
  EXPECT_TRUE(opt_cp_1.has_value());

  auto opt_cp_2 = cluster_manager_->getThreadLocalCluster("cluster_1")
                      ->tcpConnPool(ResourcePriority::Default, &context);
  EXPECT_TRUE(opt_cp_2.has_value());

  auto opt_cp_3 = cluster_manager_->getThreadLocalCluster("cluster_1")
                      ->tcpConnPool(ResourcePriority::Default, &context);
  EXPECT_TRUE(opt_cp_3.has_value());
}

TEST_F(ClusterManagerImplTest, SelectOverrideHostTestWithNonExistingHost) {
  createWithBasicStaticCluster();
  NiceMock<MockLoadBalancerContext> context;

  auto to_create = new Tcp::ConnectionPool::MockInstance();

  EXPECT_CALL(context, overrideHostToSelect())
      .WillRepeatedly(Return(absl::make_optional<Upstream::LoadBalancerContext::OverrideHost>(
          // Return non-existing host. Let the LB choose the host.
          std::make_pair("127.0.0.2:12345", false))));

  EXPECT_CALL(factory_, allocateTcpConnPool_(_))
      .WillOnce(testing::Invoke([&](HostConstSharedPtr host) {
        EXPECT_THAT(host->address()->asStringView(),
                    testing::AnyOf("127.0.0.1:11001", "127.0.0.1:11002"));
        return to_create;
      }));
  EXPECT_CALL(*to_create, addIdleCallback(_));

  auto opt_cp = cluster_manager_->getThreadLocalCluster("cluster_1")
                    ->tcpConnPool(ResourcePriority::Default, &context);
  EXPECT_TRUE(opt_cp.has_value());
}

TEST_F(ClusterManagerImplTest, SelectOverrideHostTestWithNonExistingHostStrict) {
  createWithBasicStaticCluster();
  NiceMock<MockLoadBalancerContext> context;

  EXPECT_CALL(context, overrideHostToSelect())
      .WillRepeatedly(Return(absl::make_optional<Upstream::LoadBalancerContext::OverrideHost>(
          // Return non-existing host and indicate strict mode.
          // LB should not be allowed to choose host.
          std::make_pair("127.0.0.2:12345", true))));

  // Requested upstream host 127.0.0.2:12345 is not part of the cluster.
  // Connection pool should not be created.
  EXPECT_CALL(factory_, allocateTcpConnPool_(_)).Times(0);

  auto opt_cp = cluster_manager_->getThreadLocalCluster("cluster_1")
                    ->tcpConnPool(ResourcePriority::Default, &context);
  EXPECT_FALSE(opt_cp.has_value());
}

TEST_F(ClusterManagerImplTest, UpstreamSocketOptionsPassedToConnPool) {
  createWithBasicStaticCluster();
  NiceMock<MockLoadBalancerContext> context;

  Http::ConnectionPool::MockInstance* to_create =
      new NiceMock<Http::ConnectionPool::MockInstance>();
  Network::Socket::OptionsSharedPtr options_to_return =
      Network::SocketOptionFactory::buildIpTransparentOptions();

  EXPECT_CALL(context, upstreamSocketOptions()).WillOnce(Return(options_to_return));
  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _)).WillOnce(Return(to_create));

  auto opt_cp =
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::Default, Http::Protocol::Http11, &context);
  EXPECT_TRUE(opt_cp.has_value());
}

TEST_F(ClusterManagerImplTest, UpstreamSocketOptionsUsedInConnPoolHash) {
  NiceMock<MockLoadBalancerContext> context1;
  NiceMock<MockLoadBalancerContext> context2;

  createWithBasicStaticCluster();

  // NOTE: many of these socket options are not available on Windows, so if changing the socket
  // options used below, ensure they are available on Windows and Linux, as otherwise, the option
  // won't affect the hash key (because if the socket option isn't available on the given platform,
  // an empty SocketOptionImpl is used instead) and the test will end up using the same connection
  // pool for different options, instead of different connection pool.
  // See https://github.com/envoyproxy/envoy/issues/30360 for details.
  Network::Socket::OptionsSharedPtr options1 =
      Network::SocketOptionFactory::buildTcpKeepaliveOptions({});
  Network::Socket::OptionsSharedPtr options2 =
      Network::SocketOptionFactory::buildIpPacketInfoOptions();
  Http::ConnectionPool::MockInstance* to_create1 =
      new NiceMock<Http::ConnectionPool::MockInstance>();
  Http::ConnectionPool::MockInstance* to_create2 =
      new NiceMock<Http::ConnectionPool::MockInstance>();

  EXPECT_CALL(context1, upstreamSocketOptions()).WillOnce(Return(options1));
  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _)).WillOnce(Return(to_create1));
  Http::ConnectionPool::Instance* cp1 = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::Default, Http::Protocol::Http11, &context1));
  EXPECT_NE(nullptr, cp1);

  EXPECT_CALL(context2, upstreamSocketOptions()).WillOnce(Return(options2));
  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _)).WillOnce(Return(to_create2));
  Http::ConnectionPool::Instance* cp2 = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::Default, Http::Protocol::Http11, &context2));
  EXPECT_NE(nullptr, cp2);

  // The different upstream options should lead to different hashKeys, thus different pools.
  EXPECT_NE(cp1, cp2);

  EXPECT_CALL(context1, upstreamSocketOptions()).WillOnce(Return(options1));
  Http::ConnectionPool::Instance* should_be_cp1 = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::Default, Http::Protocol::Http11, &context1));

  EXPECT_CALL(context2, upstreamSocketOptions()).WillOnce(Return(options2));
  Http::ConnectionPool::Instance* should_be_cp2 = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::Default, Http::Protocol::Http11, &context2));

  // Reusing the same options should lead to the same connection pools.
  EXPECT_EQ(cp1, should_be_cp1);
  EXPECT_EQ(cp2, should_be_cp2);
}

TEST_F(ClusterManagerImplTest, UpstreamSocketOptionsNullIsOkay) {
  createWithBasicStaticCluster();
  NiceMock<MockLoadBalancerContext> context;

  Http::ConnectionPool::MockInstance* to_create =
      new NiceMock<Http::ConnectionPool::MockInstance>();
  Network::Socket::OptionsSharedPtr options_to_return = nullptr;

  EXPECT_CALL(context, upstreamSocketOptions()).WillOnce(Return(options_to_return));
  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _)).WillOnce(Return(to_create));

  auto opt_cp =
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::Default, Http::Protocol::Http11, &context);
  EXPECT_TRUE(opt_cp.has_value());
}

TEST_F(ClusterManagerImplTest, HttpPoolDataForwardsCallsToConnectionPool) {
  createWithBasicStaticCluster();
  NiceMock<MockLoadBalancerContext> context;

  Http::ConnectionPool::MockInstance* pool_mock = new Http::ConnectionPool::MockInstance();
  Network::Socket::OptionsSharedPtr options_to_return = nullptr;

  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _)).WillOnce(Return(pool_mock));
  EXPECT_CALL(*pool_mock, addIdleCallback(_));

  auto opt_cp =
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::Default, Http::Protocol::Http11, &context);
  ASSERT_TRUE(opt_cp.has_value());

  EXPECT_CALL(*pool_mock, hasActiveConnections()).WillOnce(Return(true));
  opt_cp.value().hasActiveConnections();

  ConnectionPool::Instance::IdleCb drained_cb = []() {};
  EXPECT_CALL(*pool_mock, addIdleCallback(_));
  opt_cp.value().addIdleCallback(drained_cb);

  EXPECT_CALL(*pool_mock, drainConnections(ConnectionPool::DrainBehavior::DrainAndDelete));
  opt_cp.value().drainConnections(ConnectionPool::DrainBehavior::DrainAndDelete);
}

class TestUpstreamNetworkFilter : public Network::WriteFilter {
public:
  Network::FilterStatus onWrite(Buffer::Instance&, bool) override {
    return Network::FilterStatus::Continue;
  }
};

class TestUpstreamNetworkFilterConfigFactory
    : public Server::Configuration::NamedUpstreamNetworkFilterConfigFactory {
public:
  Network::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::UpstreamFactoryContext&) override {
    return [](Network::FilterManager& filter_manager) -> void {
      filter_manager.addWriteFilter(std::make_shared<TestUpstreamNetworkFilter>());
    };
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Struct instead of a custom per-filter empty config proto
    // This is only allowed in tests.
    return std::make_unique<Envoy::Protobuf::Struct>();
  }
  std::string name() const override { return "envoy.test.filter"; }
};

// Verify that configured upstream network filters are added to client connections.
TEST_F(ClusterManagerImplTest, AddUpstreamFilters) {
  TestUpstreamNetworkFilterConfigFactory factory;
  Registry::InjectFactory<Server::Configuration::NamedUpstreamNetworkFilterConfigFactory> registry(
      factory);
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: cluster_1
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
      filters:
      - name: envoy.test.filter
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
  )EOF";

  create(parseBootstrapFromV3Yaml(yaml));
  Network::MockClientConnection* connection = new NiceMock<Network::MockClientConnection>();
  EXPECT_CALL(*connection, addReadFilter(_)).Times(0);
  EXPECT_CALL(*connection, addWriteFilter(_));
  EXPECT_CALL(*connection, addFilter(_)).Times(0);
  EXPECT_CALL(factory_.tls_.dispatcher_, createClientConnection_(_, _, _, _))
      .WillOnce(Return(connection));
  auto conn_data = cluster_manager_->getThreadLocalCluster("cluster_1")->tcpConn(nullptr);
  EXPECT_EQ(connection, conn_data.connection_.get());
  factory_.tls_.shutdownThread();
}

class ClusterManagerInitHelperTest : public testing::Test {
public:
  MOCK_METHOD(void, onClusterInit, (ClusterManagerCluster & cluster));

  NiceMock<Config::MockXdsManager> xds_manager_;
  ClusterManagerInitHelper init_helper_{xds_manager_, [this](ClusterManagerCluster& cluster) {
                                          onClusterInit(cluster);
                                          return absl::OkStatus();
                                        }};
};

class MockClusterManagerCluster : public ClusterManagerCluster {
public:
  MockClusterManagerCluster() { ON_CALL(*this, cluster()).WillByDefault(ReturnRef(cluster_)); }

  MOCK_METHOD(Cluster&, cluster, ());
  MOCK_METHOD(LoadBalancerFactorySharedPtr, loadBalancerFactory, ());
  bool addedOrUpdated() override { return added_or_updated_; }
  void setAddedOrUpdated() override {
    ASSERT(!added_or_updated_);
    added_or_updated_ = true;
  }
  bool requiredForAds() const override { return required_for_ads_; }

  NiceMock<MockClusterMockPrioritySet> cluster_;
  bool added_or_updated_{};
  bool required_for_ads_{};
};

TEST_F(ClusterManagerInitHelperTest, ImmediateInitialize) {
  InSequence s;

  NiceMock<MockClusterManagerCluster> cluster1;
  ON_CALL(cluster1.cluster_, initializePhase())
      .WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(cluster1.cluster_, initialize(_));
  init_helper_.addCluster(cluster1);
  EXPECT_CALL(*this, onClusterInit(Ref(cluster1)));
  cluster1.cluster_.initialize_callback_();

  init_helper_.onStaticLoadComplete();
  init_helper_.startInitializingSecondaryClusters();

  ReadyWatcher cm_initialized;
  EXPECT_CALL(cm_initialized, ready());
  init_helper_.setInitializedCb([&]() -> void { cm_initialized.ready(); });
}

TEST_F(ClusterManagerInitHelperTest, StaticSdsInitialize) {
  InSequence s;

  NiceMock<MockClusterManagerCluster> sds;
  ON_CALL(sds.cluster_, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(sds.cluster_, initialize(_));
  init_helper_.addCluster(sds);
  EXPECT_CALL(*this, onClusterInit(Ref(sds)));
  sds.cluster_.initialize_callback_();

  NiceMock<MockClusterManagerCluster> cluster1;
  ON_CALL(cluster1.cluster_, initializePhase())
      .WillByDefault(Return(Cluster::InitializePhase::Secondary));
  init_helper_.addCluster(cluster1);

  init_helper_.onStaticLoadComplete();

  EXPECT_CALL(cluster1.cluster_, initialize(_));
  init_helper_.startInitializingSecondaryClusters();

  ReadyWatcher cm_initialized;
  init_helper_.setInitializedCb([&]() -> void { cm_initialized.ready(); });

  EXPECT_CALL(*this, onClusterInit(Ref(cluster1)));
  EXPECT_CALL(cm_initialized, ready());
  cluster1.cluster_.initialize_callback_();
}

// Verify that primary cluster can be updated in warming state.
TEST_F(ClusterManagerInitHelperTest, TestUpdateWarming) {
  InSequence s;

  auto sds = std::make_unique<NiceMock<MockClusterManagerCluster>>();
  ON_CALL(sds->cluster_, initializePhase())
      .WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(sds->cluster_, initialize(_));
  init_helper_.addCluster(*sds);
  init_helper_.onStaticLoadComplete();

  NiceMock<MockClusterManagerCluster> updated_sds;
  ON_CALL(updated_sds.cluster_, initializePhase())
      .WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(updated_sds.cluster_, initialize(_));
  init_helper_.addCluster(updated_sds);

  // The override cluster is added. Manually drop the previous cluster. In production flow this is
  // achieved by ClusterManagerImpl.
  sds.reset();

  ReadyWatcher primary_initialized;
  init_helper_.setPrimaryClustersInitializedCb([&]() -> void { primary_initialized.ready(); });

  EXPECT_CALL(*this, onClusterInit(Ref(updated_sds)));
  EXPECT_CALL(primary_initialized, ready());
  updated_sds.cluster_.initialize_callback_();
}

TEST_F(ClusterManagerInitHelperTest, UpdateAlreadyInitialized) {
  InSequence s;

  ReadyWatcher primary_clusters_initialized;
  init_helper_.setPrimaryClustersInitializedCb(
      [&]() -> void { primary_clusters_initialized.ready(); });
  ReadyWatcher cm_initialized;
  init_helper_.setInitializedCb([&]() -> void { cm_initialized.ready(); });

  NiceMock<MockClusterManagerCluster> cluster1;
  ON_CALL(cluster1.cluster_, initializePhase())
      .WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(cluster1.cluster_, initialize(_));
  init_helper_.addCluster(cluster1);

  NiceMock<MockClusterManagerCluster> cluster2;
  ON_CALL(cluster2.cluster_, initializePhase())
      .WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(cluster2.cluster_, initialize(_));
  init_helper_.addCluster(cluster2);

  init_helper_.onStaticLoadComplete();

  EXPECT_CALL(*this, onClusterInit(Ref(cluster1)));
  cluster1.cluster_.initialize_callback_();
  init_helper_.removeCluster(cluster1);

  EXPECT_CALL(*this, onClusterInit(Ref(cluster2)));
  EXPECT_CALL(primary_clusters_initialized, ready());
  cluster2.cluster_.initialize_callback_();

  EXPECT_CALL(cm_initialized, ready());
  init_helper_.startInitializingSecondaryClusters();
}

TEST_F(ClusterManagerInitHelperTest, RemoveUnknown) {
  InSequence s;

  NiceMock<MockClusterManagerCluster> cluster;
  init_helper_.removeCluster(cluster);
}

// If secondary clusters initialization triggered outside of CdsApiImpl::onConfigUpdate()'s
// callback flows, sending ClusterLoadAssignment should not be paused before calling
// ClusterManagerInitHelper::maybeFinishInitialize(). This case tests that
// ClusterLoadAssignment request is paused and resumed properly.
TEST_F(ClusterManagerInitHelperTest, InitSecondaryWithoutEdsPaused) {
  InSequence s;

  ReadyWatcher primary_clusters_initialized;
  init_helper_.setPrimaryClustersInitializedCb(
      [&]() -> void { primary_clusters_initialized.ready(); });
  ReadyWatcher cm_initialized;
  init_helper_.setInitializedCb([&]() -> void { cm_initialized.ready(); });

  NiceMock<MockClusterManagerCluster> cluster1;
  ON_CALL(cluster1.cluster_, initializePhase())
      .WillByDefault(Return(Cluster::InitializePhase::Secondary));
  init_helper_.addCluster(cluster1);

  EXPECT_CALL(primary_clusters_initialized, ready());
  init_helper_.onStaticLoadComplete();
  EXPECT_CALL(cluster1.cluster_, initialize(_));
  init_helper_.startInitializingSecondaryClusters();

  EXPECT_CALL(*this, onClusterInit(Ref(cluster1)));
  EXPECT_CALL(cm_initialized, ready());
  cluster1.cluster_.initialize_callback_();
}

// If secondary clusters initialization triggered inside of CdsApiImpl::onConfigUpdate()'s
// callback flows, that's, the CDS response didn't have any primary cluster, sending
// ClusterLoadAssignment should be already paused by CdsApiImpl::onConfigUpdate().
// This case tests that ClusterLoadAssignment request isn't paused again.
TEST_F(ClusterManagerInitHelperTest, InitSecondaryWithEdsPaused) {
  InSequence s;

  ReadyWatcher primary_clusters_initialized;
  init_helper_.setPrimaryClustersInitializedCb(
      [&]() -> void { primary_clusters_initialized.ready(); });
  ReadyWatcher cm_initialized;
  init_helper_.setInitializedCb([&]() -> void { cm_initialized.ready(); });

  NiceMock<MockClusterManagerCluster> cluster1;
  ON_CALL(cluster1.cluster_, initializePhase())
      .WillByDefault(Return(Cluster::InitializePhase::Secondary));
  init_helper_.addCluster(cluster1);

  EXPECT_CALL(primary_clusters_initialized, ready());
  init_helper_.onStaticLoadComplete();

  EXPECT_CALL(cluster1.cluster_, initialize(_));
  init_helper_.startInitializingSecondaryClusters();

  EXPECT_CALL(*this, onClusterInit(Ref(cluster1)));
  EXPECT_CALL(cm_initialized, ready());
  cluster1.cluster_.initialize_callback_();
}

TEST_F(ClusterManagerInitHelperTest, AddSecondaryAfterSecondaryInit) {
  InSequence s;

  ReadyWatcher primary_clusters_initialized;
  init_helper_.setPrimaryClustersInitializedCb(
      [&]() -> void { primary_clusters_initialized.ready(); });
  ReadyWatcher cm_initialized;
  init_helper_.setInitializedCb([&]() -> void { cm_initialized.ready(); });

  NiceMock<MockClusterManagerCluster> cluster1;
  ON_CALL(cluster1.cluster_, initializePhase())
      .WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(cluster1.cluster_, initialize(_));
  init_helper_.addCluster(cluster1);

  NiceMock<MockClusterManagerCluster> cluster2;
  cluster2.cluster_.info_->name_ = "cluster2";
  ON_CALL(cluster2.cluster_, initializePhase())
      .WillByDefault(Return(Cluster::InitializePhase::Secondary));
  init_helper_.addCluster(cluster2);

  init_helper_.onStaticLoadComplete();

  EXPECT_CALL(*this, onClusterInit(Ref(cluster1)));
  EXPECT_CALL(primary_clusters_initialized, ready());
  EXPECT_CALL(cluster2.cluster_, initialize(_));
  cluster1.cluster_.initialize_callback_();
  init_helper_.startInitializingSecondaryClusters();

  NiceMock<MockClusterManagerCluster> cluster3;
  cluster3.cluster_.info_->name_ = "cluster3";

  ON_CALL(cluster3.cluster_, initializePhase())
      .WillByDefault(Return(Cluster::InitializePhase::Secondary));
  EXPECT_CALL(cluster3.cluster_, initialize(_));
  init_helper_.addCluster(cluster3);

  EXPECT_CALL(*this, onClusterInit(Ref(cluster3)));
  cluster3.cluster_.initialize_callback_();
  EXPECT_CALL(*this, onClusterInit(Ref(cluster2)));
  EXPECT_CALL(cm_initialized, ready());
  cluster2.cluster_.initialize_callback_();
}

// Tests the scenario encountered in Issue 903: The cluster was removed from
// the secondary init list while traversing the list.
TEST_F(ClusterManagerInitHelperTest, RemoveClusterWithinInitLoop) {
  InSequence s;
  NiceMock<MockClusterManagerCluster> cluster;
  ON_CALL(cluster.cluster_, initializePhase())
      .WillByDefault(Return(Cluster::InitializePhase::Secondary));
  init_helper_.addCluster(cluster);

  // onStaticLoadComplete() must not initialize secondary clusters
  init_helper_.onStaticLoadComplete();

  // Set up the scenario seen in Issue 903 where initialize() ultimately results
  // in the removeCluster() call. In the real bug this was a long and complex call
  // chain.
  EXPECT_CALL(cluster.cluster_, initialize(_)).WillOnce(Invoke([&](std::function<void()>) -> void {
    init_helper_.removeCluster(cluster);
  }));

  // Now call initializeSecondaryClusters which will exercise maybeFinishInitialize()
  // which calls initialize() on the members of the secondary init list.
  init_helper_.startInitializingSecondaryClusters();
}

TEST_F(ClusterManagerImplTest, InvalidPriorityLocalClusterNameStatic) {
  std::string yaml = R"EOF(
static_resources:
  clusters:
  - name: new_cluster
    connect_timeout: 4s
    type: STATIC
    load_assignment:
      cluster_name: "domains"
      endpoints:
        - priority: 10
          lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.2
                  port_value: 11001
cluster_manager:
  local_cluster_name: new_cluster
)EOF";

  EXPECT_THROW_WITH_MESSAGE(create(parseBootstrapFromV3Yaml(yaml)), EnvoyException,
                            "Unexpected non-zero priority for local cluster 'new_cluster'.");
}

TEST_F(ClusterManagerImplTest, InvalidPriorityLocalClusterNameStrictDns) {
  std::string yaml = R"EOF(
static_resources:
  clusters:
  - name: new_cluster
    connect_timeout: 4s
    type: STRICT_DNS
    load_assignment:
      cluster_name: "domains"
      endpoints:
        - priority: 10
          lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.2
                  port_value: 11001
cluster_manager:
  local_cluster_name: new_cluster
)EOF";

  EXPECT_THROW_WITH_MESSAGE(create(parseBootstrapFromV3Yaml(yaml)), EnvoyException,
                            "Unexpected non-zero priority for local cluster 'new_cluster'.");
}

TEST_F(ClusterManagerImplTest, InvalidPriorityLocalClusterNameLogicalDns) {
  std::string yaml = R"EOF(
static_resources:
  clusters:
  - name: new_cluster
    connect_timeout: 4s
    type: LOGICAL_DNS
    load_assignment:
      cluster_name: "domains"
      endpoints:
        - priority: 10
          lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.2
                  port_value: 11001
cluster_manager:
  local_cluster_name: new_cluster
)EOF";

  // The priority for LOGICAL_DNS endpoints are written, so we just verify that there is only a
  // single priority even if the endpoint was configured to be priority 10.
  create(parseBootstrapFromV3Yaml(yaml));
  const auto cluster = cluster_manager_->getThreadLocalCluster("new_cluster");
  EXPECT_EQ(1, cluster->prioritySet().hostSetsPerPriority().size());
}

TEST_F(ClusterManagerImplTest, ConnectionPoolPerDownstreamConnection) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      connection_pool_per_downstream_connection: true
      load_assignment:
        cluster_name: cluster_1
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF";
  create(parseBootstrapFromV3Yaml(yaml));
  NiceMock<MockLoadBalancerContext> lb_context;
  NiceMock<Network::MockConnection> downstream_connection;
  Network::Socket::OptionsSharedPtr options_to_return = nullptr;
  ON_CALL(lb_context, downstreamConnection()).WillByDefault(Return(&downstream_connection));
  ON_CALL(downstream_connection, socketOptions()).WillByDefault(ReturnRef(options_to_return));

  std::vector<Http::ConnectionPool::MockInstance*> conn_pool_vector;
  for (size_t i = 0; i < 3; ++i) {
    conn_pool_vector.push_back(new Http::ConnectionPool::MockInstance());
    EXPECT_CALL(*conn_pool_vector.back(), addIdleCallback(_));
    EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _))
        .WillOnce(Return(conn_pool_vector.back()));
    EXPECT_CALL(downstream_connection, hashKey)
        .WillOnce(Invoke([i](std::vector<uint8_t>& hash_key) { hash_key.push_back(i); }));
    EXPECT_EQ(
        conn_pool_vector.back(),
        HttpPoolDataPeer::getPool(
            cluster_manager_->getThreadLocalCluster("cluster_1")
                ->httpConnPool(
                    cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
                    ResourcePriority::Default, Http::Protocol::Http11, &lb_context)));
  }

  // Check that the first entry is still in the pool map
  EXPECT_CALL(downstream_connection, hashKey).WillOnce(Invoke([](std::vector<uint8_t>& hash_key) {
    hash_key.push_back(0);
  }));
  EXPECT_EQ(
      conn_pool_vector.front(),
      HttpPoolDataPeer::getPool(
          cluster_manager_->getThreadLocalCluster("cluster_1")
              ->httpConnPool(
                  cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
                  ResourcePriority::Default, Http::Protocol::Http11, &lb_context)));
}

#ifdef ENVOY_ENABLE_QUIC
TEST_F(ClusterManagerImplTest, PassDownNetworkObserverRegistryToConnectionPool) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      connection_pool_per_downstream_connection: true
      load_assignment:
        cluster_name: cluster_1
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF";
  create(parseBootstrapFromV3Yaml(yaml));
  auto cluster1 = cluster_manager_->getThreadLocalCluster("cluster_1");

  const std::string cluster_api = R"EOF(
    name: added_via_api
    connect_timeout: 0.250s
    type: STATIC
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: added_via_api
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 11001
  )EOF";
  // Add static cluster via api and check that addresses list is empty.
  EXPECT_TRUE(*cluster_manager_->addOrUpdateCluster(parseClusterFromV3Yaml(cluster_api), "v1"));
  auto cluster_added_via_api = cluster_manager_->getThreadLocalCluster("added_via_api");

  Quic::TestEnvoyQuicNetworkObserverRegistryFactory registry_factory;
  cluster_manager_->createNetworkObserverRegistries(registry_factory);

  NiceMock<MockLoadBalancerContext> lb_context;
  NiceMock<Network::MockConnection> downstream_connection;
  Network::Socket::OptionsSharedPtr options_to_return = nullptr;
  ON_CALL(lb_context, downstreamConnection()).WillByDefault(Return(&downstream_connection));
  ON_CALL(downstream_connection, socketOptions()).WillByDefault(ReturnRef(options_to_return));

  auto* pool = new Http::ConnectionPool::MockInstance();
  Quic::EnvoyQuicNetworkObserverRegistry* created_registry = nullptr;
  EXPECT_CALL(*pool, addIdleCallback(_));
  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _))
      .WillOnce(testing::WithArg<5>(
          Invoke([pool, created_registry_ptr = &created_registry](
                     OptRef<Quic::EnvoyQuicNetworkObserverRegistry> network_observer_registry) {
            EXPECT_TRUE(network_observer_registry.has_value());
            *created_registry_ptr = network_observer_registry.ptr();
            return pool;
          })));
  EXPECT_CALL(downstream_connection, hashKey).WillOnce(Invoke([](std::vector<uint8_t>& hash_key) {
    hash_key.push_back(0);
  }));
  EXPECT_EQ(pool,
            HttpPoolDataPeer::getPool(cluster1->httpConnPool(
                cluster_manager_->getThreadLocalCluster("added_via_api")->chooseHost(nullptr).host,
                ResourcePriority::Default, Http::Protocol::Http11, &lb_context)));

  pool = new Http::ConnectionPool::MockInstance();
  EXPECT_CALL(*pool, addIdleCallback(_));
  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _))
      .WillOnce(testing::WithArg<5>(
          Invoke([pool, created_registry](
                     OptRef<Quic::EnvoyQuicNetworkObserverRegistry> network_observer_registry) {
            EXPECT_TRUE(network_observer_registry.has_value());
            EXPECT_EQ(created_registry, network_observer_registry.ptr());
            return pool;
          })));
  EXPECT_EQ(pool,
            HttpPoolDataPeer::getPool(cluster_added_via_api->httpConnPool(
                cluster_manager_->getThreadLocalCluster("added_via_api")->chooseHost(nullptr).host,
                ResourcePriority::Default, Http::Protocol::Http11, &lb_context)));
}

#endif

TEST_F(ClusterManagerImplTest, ConnectionPoolPerDownstreamConnection_tcp) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      connection_pool_per_downstream_connection: true
      load_assignment:
        cluster_name: cluster_1
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF";
  create(parseBootstrapFromV3Yaml(yaml));
  NiceMock<MockLoadBalancerContext> lb_context;
  NiceMock<Network::MockConnection> downstream_connection;
  Network::Socket::OptionsSharedPtr options_to_return = nullptr;
  ON_CALL(lb_context, downstreamConnection()).WillByDefault(Return(&downstream_connection));
  ON_CALL(downstream_connection, socketOptions()).WillByDefault(ReturnRef(options_to_return));

  std::vector<Tcp::ConnectionPool::MockInstance*> conn_pool_vector;
  for (size_t i = 0; i < 3; ++i) {
    conn_pool_vector.push_back(new Tcp::ConnectionPool::MockInstance());
    EXPECT_CALL(*conn_pool_vector.back(), addIdleCallback(_));
    EXPECT_CALL(factory_, allocateTcpConnPool_(_)).WillOnce(Return(conn_pool_vector.back()));
    EXPECT_CALL(downstream_connection, hashKey)
        .WillOnce(Invoke([i](std::vector<uint8_t>& hash_key) { hash_key.push_back(i); }));
    EXPECT_EQ(conn_pool_vector.back(),
              TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                           ->tcpConnPool(ResourcePriority::Default, &lb_context)));
  }

  // Check that the first entry is still in the pool map
  EXPECT_CALL(downstream_connection, hashKey).WillOnce(Invoke([](std::vector<uint8_t>& hash_key) {
    hash_key.push_back(0);
  }));
  EXPECT_EQ(conn_pool_vector.front(),
            TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                         ->tcpConnPool(ResourcePriority::Default, &lb_context)));
}

TEST_F(ClusterManagerImplTest, CheckAddressesList) {
  const std::string bootstrap = R"EOF(
  static_resources:
    clusters:
    - name: cluster_0
      connect_timeout: 0.250s
      type: STATIC
      lb_policy: ROUND_ROBIN
      load_assignment:
        cluster_name: cluster_0
        endpoints:
        - lb_endpoints:
          - endpoint:
              additionalAddresses:
              - address:
                  socketAddress:
                    address: ::1
                    portValue: 11001
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF";
  create(parseBootstrapFromV3Yaml(bootstrap));
  // Verify address list for static cluster in bootstrap.
  auto cluster = cluster_manager_->getThreadLocalCluster("cluster_0");
  auto hosts = cluster->prioritySet().hostSetsPerPriority()[0]->hosts();
  ASSERT_NE(hosts[0]->addressListOrNull(), nullptr);
  ASSERT_EQ(hosts[0]->addressListOrNull()->size(), 2);

  const std::string cluster_api = R"EOF(
    name: added_via_api
    connect_timeout: 0.250s
    type: STATIC
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: added_via_api
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 11001
  )EOF";
  const std::string cluster_api_update = R"EOF(
    name: added_via_api
    connect_timeout: 0.250s
    type: STATIC
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: added_via_api
      endpoints:
      - lb_endpoints:
        - endpoint:
            additionalAddresses:
              - address:
                  socketAddress:
                    address: ::1
                    portValue: 11001
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 11001
  )EOF";
  // Add static cluster via api and check that addresses list is empty.
  EXPECT_TRUE(*cluster_manager_->addOrUpdateCluster(parseClusterFromV3Yaml(cluster_api), "v1"));
  cluster = cluster_manager_->getThreadLocalCluster("added_via_api");
  hosts = cluster->prioritySet().hostSetsPerPriority()[0]->hosts();
  ASSERT_EQ(hosts[0]->addressListOrNull(), nullptr);
  // Update cluster to have additional addresses and check that address list is not empty anymore.
  EXPECT_TRUE(
      *cluster_manager_->addOrUpdateCluster(parseClusterFromV3Yaml(cluster_api_update), "v2"));
  cluster = cluster_manager_->getThreadLocalCluster("added_via_api");
  hosts = cluster->prioritySet().hostSetsPerPriority()[0]->hosts();
  ASSERT_NE(hosts[0]->addressListOrNull(), nullptr);
  ASSERT_EQ(hosts[0]->addressListOrNull()->size(), 2);
}

// Verify that non-IP additional addresses are rejected.
TEST_F(ClusterManagerImplTest, RejectNonIpAdditionalAddresses) {
  const std::string bootstrap = R"EOF(
  static_resources:
    clusters:
    - name: cluster_0
      connect_timeout: 0.250s
      type: STATIC
      lb_policy: ROUND_ROBIN
      load_assignment:
        cluster_name: cluster_0
        endpoints:
        - lb_endpoints:
          - endpoint:
              additionalAddresses:
              - address:
                  envoyInternalAddress:
                   server_listener_name: internal_address
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF";
  try {
    create(parseBootstrapFromV3Yaml(bootstrap));
    FAIL() << "Invalid address was not rejected";
  } catch (const EnvoyException& e) {
    EXPECT_STREQ("additional_addresses must be IP addresses.", e.what());
  }
}

TEST_F(ClusterManagerImplTest, CheckActiveStaticCluster) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: good
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: good
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF";
  create(parseBootstrapFromV3Yaml(yaml));
  const std::string added_via_api_yaml = R"EOF(
    name: added_via_api
    connect_timeout: 0.250s
    type: STATIC
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: added_via_api
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 11001
  )EOF";
  EXPECT_TRUE(
      *cluster_manager_->addOrUpdateCluster(parseClusterFromV3Yaml(added_via_api_yaml), "v1"));

  EXPECT_EQ(2, cluster_manager_->clusters().active_clusters_.size());
  EXPECT_TRUE(cluster_manager_->checkActiveStaticCluster("good").ok());
  EXPECT_EQ(cluster_manager_->checkActiveStaticCluster("nonexist").message(),
            "Unknown gRPC client cluster 'nonexist'");
  EXPECT_EQ(cluster_manager_->checkActiveStaticCluster("added_via_api").message(),
            "gRPC client cluster 'added_via_api' is not static");
}

TEST_F(ClusterManagerImplTest, ClusterIgnoreRemoval) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: good
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: good
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF";
  create(parseBootstrapFromV3Yaml(yaml));
  const std::string added_via_api_yaml = R"EOF(
    name: added_via_api
    connect_timeout: 0.250s
    type: STATIC
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: added_via_api
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 11001
  )EOF";
  auto cluster = parseClusterFromV3Yaml(added_via_api_yaml);

  EXPECT_TRUE(*cluster_manager_->addOrUpdateCluster(cluster, "v1", true));

  EXPECT_EQ(2, cluster_manager_->clusters().active_clusters_.size());
  EXPECT_TRUE(cluster_manager_->checkActiveStaticCluster("good").ok());

  // This should not remove the cluster as remove_ignored is set to false
  EXPECT_FALSE(cluster_manager_->removeCluster("added_via_api"));
  EXPECT_EQ(2, cluster_manager_->clusters().active_clusters_.size());

  // This should remove the cluster as remove_ignored is set to true
  EXPECT_TRUE(cluster_manager_->removeCluster("added_via_api", true));
  EXPECT_EQ(1, cluster_manager_->clusters().active_clusters_.size());
}

#ifdef WIN32
TEST_F(ClusterManagerImplTest, LocalInterfaceNameForUpstreamConnectionThrowsInWin32) {
  const std::string yaml = fmt::format(R"EOF(
 static_resources:
  clusters:
  - name: cluster_1
    connect_timeout: 0.25s
    type: STATIC
    lb_policy: ROUND_ROBIN
    upstream_connection_options:
      set_local_interface_name_on_upstream_connections: true
    load_assignment:
      cluster_name: cluster_1
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 5678
  )EOF");

  EXPECT_THROW_WITH_MESSAGE(create(parseBootstrapFromV3Yaml(yaml)), EnvoyException,
                            "set_local_interface_name_on_upstream_connections_ cannot be set to "
                            "true on Windows platforms");
}
#endif

} // namespace
} // namespace Upstream
} // namespace Envoy
