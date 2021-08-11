#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.validate.h"
#include "envoy/config/core/v3/base.pb.h"

#include "source/common/network/raw_buffer_socket.h"
#include "source/common/network/resolver_impl.h"
#include "source/common/router/context_impl.h"
#include "source/extensions/transport_sockets/raw_buffer/config.h"

#include "test/common/upstream/test_cluster_manager.h"
#include "test/config/v2_link_hacks.h"
#include "test/mocks/http/conn_pool.h"
#include "test/mocks/matcher/mocks.h"
#include "test/mocks/upstream/cds_api.h"
#include "test/mocks/upstream/cluster_priority_set.h"
#include "test/mocks/upstream/cluster_real_priority_set.h"
#include "test/mocks/upstream/cluster_update_callbacks.h"
#include "test/mocks/upstream/health_checker.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/mocks/upstream/thread_aware_load_balancer.h"
#include "test/test_common/test_runtime.h"

namespace Envoy {
namespace Upstream {

class HttpPoolDataPeer {
public:
  static Http::ConnectionPool::MockInstance* getPool(absl::optional<HttpPoolData> data) {
    ASSERT(data.has_value());
    return dynamic_cast<Http::ConnectionPool::MockInstance*>(data.value().pool_);
  }
};

class TcpPoolDataPeer {
public:
  static Tcp::ConnectionPool::MockInstance* getPool(absl::optional<TcpPoolData> data) {
    ASSERT(data.has_value());
    return dynamic_cast<Tcp::ConnectionPool::MockInstance*>(data.value().pool_);
  }
};

namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::Eq;
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

envoy::config::bootstrap::v3::Bootstrap parseBootstrapFromV3Yaml(const std::string& yaml,
                                                                 bool avoid_boosting = true) {
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  TestUtility::loadFromYaml(yaml, bootstrap, true, avoid_boosting);
  return bootstrap;
}

std::string clustersJson(const std::vector<std::string>& clusters) {
  return fmt::sprintf("\"clusters\": [%s]", absl::StrJoin(clusters, ","));
}

class ClusterManagerImplTest : public testing::Test {
public:
  ClusterManagerImplTest()
      : http_context_(factory_.stats_.symbolTable()), grpc_context_(factory_.stats_.symbolTable()),
        router_context_(factory_.stats_.symbolTable()) {}

  void create(const envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    cluster_manager_ = std::make_unique<TestClusterManagerImpl>(
        bootstrap, factory_, factory_.stats_, factory_.tls_, factory_.runtime_,
        factory_.local_info_, log_manager_, factory_.dispatcher_, admin_, validation_context_,
        *factory_.api_, http_context_, grpc_context_, router_context_);
    cluster_manager_->setPrimaryClustersInitializedCb(
        [this, bootstrap]() { cluster_manager_->initializeSecondaryClusters(bootstrap); });
  }

  void createWithLocalClusterUpdate(const bool enable_merge_window = true) {
    std::string yaml = R"EOF(
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
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11002
  )EOF";
    const std::string merge_window_enabled = R"EOF(
      common_lb_config:
        update_merge_window: 3s
  )EOF";
    const std::string merge_window_disabled = R"EOF(
      common_lb_config:
        update_merge_window: 0s
  )EOF";

    yaml += enable_merge_window ? merge_window_enabled : merge_window_disabled;

    const auto& bootstrap = parseBootstrapFromV3Yaml(yaml);

    cluster_manager_ = std::make_unique<MockedUpdatedClusterManagerImpl>(
        bootstrap, factory_, factory_.stats_, factory_.tls_, factory_.runtime_,
        factory_.local_info_, log_manager_, factory_.dispatcher_, admin_, validation_context_,
        *factory_.api_, local_cluster_update_, local_hosts_removed_, http_context_, grpc_context_,
        router_context_);
  }

  void checkStats(uint64_t added, uint64_t modified, uint64_t removed, uint64_t active,
                  uint64_t warming) {
    EXPECT_EQ(added, factory_.stats_.counter("cluster_manager.cluster_added").value());
    EXPECT_EQ(modified, factory_.stats_.counter("cluster_manager.cluster_modified").value());
    EXPECT_EQ(removed, factory_.stats_.counter("cluster_manager.cluster_removed").value());
    EXPECT_EQ(active,
              factory_.stats_
                  .gauge("cluster_manager.active_clusters", Stats::Gauge::ImportMode::NeverImport)
                  .value());
    EXPECT_EQ(warming,
              factory_.stats_
                  .gauge("cluster_manager.warming_clusters", Stats::Gauge::ImportMode::NeverImport)
                  .value());
  }

  void checkConfigDump(
      const std::string& expected_dump_yaml,
      const Matchers::StringMatcher& name_matcher = Matchers::UniversalStringMatcher()) {
    auto message_ptr = admin_.config_tracker_.config_tracker_callbacks_["clusters"](name_matcher);
    const auto& clusters_config_dump =
        dynamic_cast<const envoy::admin::v3::ClustersConfigDump&>(*message_ptr);

    envoy::admin::v3::ClustersConfigDump expected_clusters_config_dump;
    TestUtility::loadFromYaml(expected_dump_yaml, expected_clusters_config_dump);
    EXPECT_EQ(expected_clusters_config_dump.DebugString(), clusters_config_dump.DebugString());
  }

  MetadataConstSharedPtr buildMetadata(const std::string& version) const {
    envoy::config::core::v3::Metadata metadata;

    if (!version.empty()) {
      Envoy::Config::Metadata::mutableMetadataValue(
          metadata, Config::MetadataFilters::get().ENVOY_LB, "version")
          .set_string_value(version);
    }

    return std::make_shared<const envoy::config::core::v3::Metadata>(metadata);
  }

  Event::SimulatedTimeSystem time_system_;
  NiceMock<TestClusterManagerFactory> factory_;
  NiceMock<ProtobufMessage::MockValidationContext> validation_context_;
  std::unique_ptr<TestClusterManagerImpl> cluster_manager_;
  AccessLog::MockAccessLogManager log_manager_;
  NiceMock<Server::MockAdmin> admin_;
  MockLocalClusterUpdate local_cluster_update_;
  MockLocalHostsRemoved local_hosts_removed_;
  Http::ContextImpl http_context_;
  Grpc::ContextImpl grpc_context_;
  Router::ContextImpl router_context_;
};

MATCHER_P(CustomDnsResolversSizeEquals, expectedResolvers, "") {
  return expectedResolvers.size() == arg.size();
}

envoy::config::bootstrap::v3::Bootstrap defaultConfig() {
  const std::string yaml = R"EOF(
static_resources:
  clusters: []
  )EOF";

  return parseBootstrapFromV3Yaml(yaml);
}

class AlpnSocketFactory : public Network::RawBufferSocketFactory {
public:
  bool supportsAlpn() const override { return true; }
};

class AlpnTestConfigFactory
    : public Envoy::Extensions::TransportSockets::RawBuffer::UpstreamRawBufferSocketFactory {
public:
  std::string name() const override { return "envoy.transport_sockets.alpn"; }
  Network::TransportSocketFactoryPtr
  createTransportSocketFactory(const Protobuf::Message&,
                               Server::Configuration::TransportSocketFactoryContext&) override {
    return std::make_unique<AlpnSocketFactory>();
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

  EXPECT_CALL(log_manager_, createAccessLog(Filesystem::FilePathAndType{
                                Filesystem::DestinationType::File, "foo"}));
  create(parseBootstrapFromV3Json(json));
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

  EXPECT_THROW_WITH_REGEX(create(parseBootstrapFromV3Json(json)), EnvoyException,
                          "invalid value \"foo\"");
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

  EXPECT_THROW_WITH_REGEX(create(parseBootstrapFromV3Json(json)), EnvoyException,
                          "fake_property: Cannot find field");
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
          transport_api_version: V3
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

class ClusterManagerSubsetInitializationTest
    : public ClusterManagerImplTest,
      public testing::WithParamInterface<envoy::config::cluster::v3::Cluster::LbPolicy> {
public:
  ClusterManagerSubsetInitializationTest() = default;

  static std::vector<envoy::config::cluster::v3::Cluster::LbPolicy> lbPolicies() {
    int first = static_cast<int>(envoy::config::cluster::v3::Cluster::LbPolicy_MIN);
    int last = static_cast<int>(envoy::config::cluster::v3::Cluster::LbPolicy_MAX);
    ASSERT(first < last);

    std::vector<envoy::config::cluster::v3::Cluster::LbPolicy> policies;
    for (int i = first; i <= last; i++) {
      if (envoy::config::cluster::v3::Cluster::LbPolicy_IsValid(i)) {
        auto policy = static_cast<envoy::config::cluster::v3::Cluster::LbPolicy>(i);
        if (policy !=
                envoy::config::cluster::v3::Cluster::hidden_envoy_deprecated_ORIGINAL_DST_LB &&
            policy != envoy::config::cluster::v3::Cluster::LOAD_BALANCING_POLICY_CONFIG) {
          policies.push_back(policy);
        }
      }
    }
    return policies;
  }

  static std::string paramName(const testing::TestParamInfo<ParamType>& info) {
    const std::string& name = envoy::config::cluster::v3::Cluster::LbPolicy_Name(info.param);
    return absl::StrReplaceAll(name, {{"_", ""}});
  }
};

// Test initialization of subset load balancer with every possible load balancer policy.
TEST_P(ClusterManagerSubsetInitializationTest, SubsetLoadBalancerInitialization) {
  const std::string yamlPattern = R"EOF(
 static_resources:
  clusters:
  - name: cluster_1
    connect_timeout: 0.250s
    {}
    lb_policy: "{}"
    lb_subset_config:
      fallback_policy: ANY_ENDPOINT
      subset_selectors:
        - keys: [ "x" ]
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

  const std::string& policy_name = envoy::config::cluster::v3::Cluster::LbPolicy_Name(GetParam());

  std::string cluster_type = "type: STATIC";

  if (GetParam() == envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED) {
    // This custom cluster type is registered by linking test/integration/custom/static_cluster.cc.
    cluster_type = "cluster_type: { name: envoy.clusters.custom_static_with_lb }";
  }
  const std::string yaml = fmt::format(yamlPattern, cluster_type, policy_name);

  if (GetParam() == envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED) {
    EXPECT_THROW_WITH_MESSAGE(
        create(parseBootstrapFromV3Yaml(yaml)), EnvoyException,
        fmt::format("cluster: LB policy {} cannot be combined with lb_subset_config",
                    envoy::config::cluster::v3::Cluster::LbPolicy_Name(GetParam())));

  } else {
    create(parseBootstrapFromV3Yaml(yaml));
    checkStats(1 /*added*/, 0 /*modified*/, 0 /*removed*/, 1 /*active*/, 0 /*warming*/);

    Upstream::ThreadLocalCluster* tlc = cluster_manager_->getThreadLocalCluster("cluster_1");
    EXPECT_NE(nullptr, tlc);

    if (tlc) {
      Upstream::LoadBalancer& lb = tlc->loadBalancer();
      EXPECT_NE(nullptr, dynamic_cast<Upstream::SubsetLoadBalancer*>(&lb));
    }

    factory_.tls_.shutdownThread();
  }
}

INSTANTIATE_TEST_SUITE_P(ClusterManagerSubsetInitializationTest,
                         ClusterManagerSubsetInitializationTest,
                         testing::ValuesIn(ClusterManagerSubsetInitializationTest::lbPolicies()),
                         ClusterManagerSubsetInitializationTest::paramName);

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
  cluster1->info_->lb_type_ = LoadBalancerType::ClusterProvided;
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _))
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
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _))
      .WillOnce(Return(std::make_pair(cluster1, new MockThreadAwareLoadBalancer())));
  EXPECT_THROW_WITH_MESSAGE(create(parseBootstrapFromV3Json(json)), EnvoyException,
                            "cluster manager: cluster provided LB not specified but cluster "
                            "'cluster_0' provided one. Check cluster documentation.");
}

class ClusterManagerImplThreadAwareLbTest : public ClusterManagerImplTest {
public:
  void doTest(LoadBalancerType lb_type) {
    const std::string json = fmt::sprintf("{\"static_resources\":{%s}}",
                                          clustersJson({defaultStaticClusterJson("cluster_0")}));

    std::shared_ptr<MockClusterMockPrioritySet> cluster1(
        new NiceMock<MockClusterMockPrioritySet>());
    cluster1->info_->name_ = "cluster_0";
    cluster1->info_->lb_type_ = lb_type;

    InSequence s;
    EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _))
        .WillOnce(Return(std::make_pair(cluster1, nullptr)));
    ON_CALL(*cluster1, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
    create(parseBootstrapFromV3Json(json));

    EXPECT_EQ(nullptr, cluster_manager_->getThreadLocalCluster("cluster_0"));

    cluster1->prioritySet().getMockHostSet(0)->hosts_ = {
        makeTestHost(cluster1->info_, "tcp://127.0.0.1:80", time_system_)};
    cluster1->prioritySet().getMockHostSet(0)->runCallbacks(
        cluster1->prioritySet().getMockHostSet(0)->hosts_, {});
    cluster1->initialize_callback_();
    EXPECT_EQ(
        cluster1->prioritySet().getMockHostSet(0)->hosts_[0],
        cluster_manager_->getThreadLocalCluster("cluster_0")->loadBalancer().chooseHost(nullptr));
  }
};

// Test that the cluster manager correctly re-creates the worker local LB when there is a host
// set change.
TEST_F(ClusterManagerImplThreadAwareLbTest, RingHashLoadBalancerThreadAwareUpdate) {
  doTest(LoadBalancerType::RingHash);
}

// Test that the cluster manager correctly re-creates the worker local LB when there is a host
// set change.
TEST_F(ClusterManagerImplThreadAwareLbTest, MaglevLoadBalancerThreadAwareUpdate) {
  doTest(LoadBalancerType::Maglev);
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
                  PointeesEq(Network::Utility::resolveUrl("tcp://127.0.0.1:11001")), _, _, _))
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
                  PointeesEq(Network::Utility::resolveUrl("tcp://127.0.0.1:11001")), _, _, _))
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

TEST_F(ClusterManagerImplTest, ShutdownOrder) {
  const std::string json = fmt::sprintf("{\"static_resources\":{%s}}",
                                        clustersJson({defaultStaticClusterJson("cluster_1")}));

  create(parseBootstrapFromV3Json(json));
  Cluster& cluster = cluster_manager_->activeClusters().begin()->second;
  EXPECT_EQ("cluster_1", cluster.info()->name());
  EXPECT_EQ(cluster.info(), cluster_manager_->getThreadLocalCluster("cluster_1")->info());
  EXPECT_EQ(1UL, cluster_manager_->getThreadLocalCluster("cluster_1")
                     ->prioritySet()
                     .hostSetsPerPriority()[0]
                     ->hosts()
                     .size());
  EXPECT_EQ(
      cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0],
      cluster_manager_->getThreadLocalCluster("cluster_1")->loadBalancer().chooseHost(nullptr));

  // Local reference, primary reference, thread local reference, host reference, async client
  // reference.
  EXPECT_EQ(5U, cluster.info().use_count());

  // Thread local reference should be gone.
  factory_.tls_.shutdownThread();
  EXPECT_EQ(3U, cluster.info().use_count());
}

TEST_F(ClusterManagerImplTest, InitializeOrder) {
  time_system_.setSystemTime(std::chrono::milliseconds(1234567891234));

  const std::string json = fmt::sprintf(
      R"EOF(
  {
    "dynamic_resources": {
      "cds_config": {
        "api_config_source": {
          "api_type": "0",
          "refresh_delay": "30s",
          "cluster_names": ["cds_cluster"]
        }
      }
    },
    "static_resources": {
      %s
    }
  }
  )EOF",
      clustersJson({defaultStaticClusterJson("cds_cluster"),
                    defaultStaticClusterJson("fake_cluster"),
                    defaultStaticClusterJson("fake_cluster2")}));

  MockCdsApi* cds = new MockCdsApi();
  std::shared_ptr<MockClusterMockPrioritySet> cds_cluster(
      new NiceMock<MockClusterMockPrioritySet>());
  cds_cluster->info_->name_ = "cds_cluster";
  std::shared_ptr<MockClusterMockPrioritySet> cluster1(new NiceMock<MockClusterMockPrioritySet>());
  std::shared_ptr<MockClusterMockPrioritySet> cluster2(new NiceMock<MockClusterMockPrioritySet>());
  cluster2->info_->name_ = "fake_cluster2";
  cluster2->info_->lb_type_ = LoadBalancerType::RingHash;

  // This part tests static init.
  InSequence s;
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _))
      .WillOnce(Return(std::make_pair(cds_cluster, nullptr)));
  ON_CALL(*cds_cluster, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _))
      .WillOnce(Return(std::make_pair(cluster1, nullptr)));
  ON_CALL(*cluster1, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _))
      .WillOnce(Return(std::make_pair(cluster2, nullptr)));
  ON_CALL(*cluster2, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Secondary));
  EXPECT_CALL(factory_, createCds_()).WillOnce(Return(cds));
  EXPECT_CALL(*cds, setInitializedCb(_));
  EXPECT_CALL(*cds_cluster, initialize(_));
  EXPECT_CALL(*cluster1, initialize(_));

  create(parseBootstrapFromV3Json(json));

  ReadyWatcher initialized;
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

  EXPECT_CALL(*cluster2, initialize(_));
  cds_cluster->initialize_callback_();
  cluster1->initialize_callback_();

  EXPECT_CALL(*cds, initialize());
  cluster2->initialize_callback_();

  // This part tests CDS init.
  std::shared_ptr<MockClusterMockPrioritySet> cluster3(new NiceMock<MockClusterMockPrioritySet>());
  cluster3->info_->name_ = "cluster3";
  std::shared_ptr<MockClusterMockPrioritySet> cluster4(new NiceMock<MockClusterMockPrioritySet>());
  cluster4->info_->name_ = "cluster4";
  std::shared_ptr<MockClusterMockPrioritySet> cluster5(new NiceMock<MockClusterMockPrioritySet>());
  cluster5->info_->name_ = "cluster5";

  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _))
      .WillOnce(Return(std::make_pair(cluster3, nullptr)));
  ON_CALL(*cluster3, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Secondary));
  cluster_manager_->addOrUpdateCluster(defaultStaticCluster("cluster3"), "version1");

  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _))
      .WillOnce(Return(std::make_pair(cluster4, nullptr)));
  ON_CALL(*cluster4, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(*cluster4, initialize(_));
  cluster_manager_->addOrUpdateCluster(defaultStaticCluster("cluster4"), "version2");

  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _))
      .WillOnce(Return(std::make_pair(cluster5, nullptr)));
  ON_CALL(*cluster5, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Secondary));
  cluster_manager_->addOrUpdateCluster(defaultStaticCluster("cluster5"), "version3");

  cds->initialized_callback_();
  EXPECT_CALL(*cds, versionInfo()).WillOnce(Return("version3"));
  checkConfigDump(R"EOF(
 version_info: version3
 static_clusters:
  - cluster:
      "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
      name: "cds_cluster"
      type: "STATIC"
      connect_timeout: 0.25s
      load_assignment:
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
    last_updated:
      seconds: 1234567891
      nanos: 234000000
  - cluster:
      "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
      name: "fake_cluster"
      type: "STATIC"
      connect_timeout: 0.25s
      load_assignment:
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
    last_updated:
      seconds: 1234567891
      nanos: 234000000
  - cluster:
      "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
      name: "fake_cluster2"
      type: "STATIC"
      connect_timeout: 0.25s
      load_assignment:
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
    last_updated:
      seconds: 1234567891
      nanos: 234000000
 dynamic_warming_clusters:
  - version_info: "version1"
    cluster:
      "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
      name: "cluster3"
      type: "STATIC"
      connect_timeout: 0.25s
      load_assignment:
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
    last_updated:
      seconds: 1234567891
      nanos: 234000000
  - version_info: "version2"
    cluster:
      "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
      name: "cluster4"
      type: "STATIC"
      connect_timeout: 0.25s
      load_assignment:
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
    last_updated:
      seconds: 1234567891
      nanos: 234000000
  - version_info: "version3"
    cluster:
      "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
      name: "cluster5"
      type: "STATIC"
      connect_timeout: 0.25s
      load_assignment:
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
    last_updated:
      seconds: 1234567891
      nanos: 234000000
 dynamic_active_clusters:
)EOF");

  EXPECT_CALL(*cluster3, initialize(_));
  cluster4->initialize_callback_();

  // Test cluster 5 getting removed before everything is initialized.
  cluster_manager_->removeCluster("cluster5");

  EXPECT_CALL(initialized, ready());
  cluster3->initialize_callback_();

  factory_.tls_.shutdownThread();

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cds_cluster.get()));
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster1.get()));
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster2.get()));
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster3.get()));
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster4.get()));
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster5.get()));
}

TEST_F(ClusterManagerImplTest, DynamicRemoveWithLocalCluster) {
  InSequence s;

  // Setup a cluster manager with a static local cluster.
  const std::string json = fmt::sprintf(R"EOF(
  {
    "cluster_manager": {
      "local_cluster_name": "foo"
    },
    "static_resources": {
      %s
    }
  }
  )EOF",
                                        clustersJson({defaultStaticClusterJson("fake")}));

  std::shared_ptr<MockClusterMockPrioritySet> foo(new NiceMock<MockClusterMockPrioritySet>());
  foo->info_->name_ = "foo";
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, false))
      .WillOnce(Return(std::make_pair(foo, nullptr)));
  ON_CALL(*foo, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(*foo, initialize(_));

  create(parseBootstrapFromV3Json(json));
  foo->initialize_callback_();

  // Now add a dynamic cluster. This cluster will have a member update callback from the local
  // cluster in its load balancer.
  std::shared_ptr<MockClusterMockPrioritySet> cluster1(new NiceMock<MockClusterMockPrioritySet>());
  cluster1->info_->name_ = "cluster1";
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, true))
      .WillOnce(Return(std::make_pair(cluster1, nullptr)));
  ON_CALL(*cluster1, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(*cluster1, initialize(_));
  cluster_manager_->addOrUpdateCluster(defaultStaticCluster("cluster1"), "");

  // Add another update callback on foo so we make sure callbacks keep working.
  ReadyWatcher membership_updated;
  auto priority_update_cb = foo->prioritySet().addPriorityUpdateCb(
      [&membership_updated](uint32_t, const HostVector&, const HostVector&) -> void {
        membership_updated.ready();
      });

  // Remove the new cluster.
  cluster_manager_->removeCluster("cluster1");

  // Fire a member callback on the local cluster, which should not call any update callbacks on
  // the deleted cluster.
  foo->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(foo->info_, "tcp://127.0.0.1:80", time_system_)};
  EXPECT_CALL(membership_updated, ready());
  foo->prioritySet().getMockHostSet(0)->runCallbacks(foo->prioritySet().getMockHostSet(0)->hosts_,
                                                     {});

  factory_.tls_.shutdownThread();

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(foo.get()));
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster1.get()));
}

TEST_F(ClusterManagerImplTest, RemoveWarmingCluster) {
  time_system_.setSystemTime(std::chrono::milliseconds(1234567891234));
  create(defaultConfig());

  InSequence s;
  ReadyWatcher initialized;
  EXPECT_CALL(initialized, ready());
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

  std::shared_ptr<MockClusterMockPrioritySet> cluster1(new NiceMock<MockClusterMockPrioritySet>());
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _))
      .WillOnce(Return(std::make_pair(cluster1, nullptr)));
  EXPECT_CALL(*cluster1, initializePhase()).Times(0);
  EXPECT_CALL(*cluster1, initialize(_));
  EXPECT_TRUE(
      cluster_manager_->addOrUpdateCluster(defaultStaticCluster("fake_cluster"), "version3"));
  checkStats(1 /*added*/, 0 /*modified*/, 0 /*removed*/, 0 /*active*/, 1 /*warming*/);
  EXPECT_EQ(nullptr, cluster_manager_->getThreadLocalCluster("fake_cluster"));
  checkConfigDump(R"EOF(
dynamic_warming_clusters:
  - version_info: "version3"
    cluster:
      "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
      name: "fake_cluster"
      type: STATIC
      connect_timeout: 0.25s
      load_assignment:
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
    last_updated:
      seconds: 1234567891
      nanos: 234000000
)EOF");

  EXPECT_TRUE(cluster_manager_->removeCluster("fake_cluster"));
  checkStats(1 /*added*/, 0 /*modified*/, 1 /*removed*/, 0 /*active*/, 0 /*warming*/);

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster1.get()));
}

TEST_F(ClusterManagerImplTest, TestModifyWarmingClusterDuringInitialization) {
  const std::string json = fmt::sprintf(
      R"EOF(
  {
    "dynamic_resources": {
      "cds_config": {
        "api_config_source": {
          "api_type": "0",
          "refresh_delay": "30s",
          "cluster_names": ["cds_cluster"]
        }
      }
    },
    "static_resources": {
      %s
    }
  }
  )EOF",
      clustersJson({
          defaultStaticClusterJson("cds_cluster"),
      }));

  MockCdsApi* cds = new MockCdsApi();
  std::shared_ptr<MockClusterMockPrioritySet> cds_cluster(
      new NiceMock<MockClusterMockPrioritySet>());
  cds_cluster->info_->name_ = "cds_cluster";

  // This part tests static init.
  InSequence s;
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _))
      .WillOnce(Return(std::make_pair(cds_cluster, nullptr)));
  ON_CALL(*cds_cluster, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(factory_, createCds_()).WillOnce(Return(cds));
  EXPECT_CALL(*cds, setInitializedCb(_));
  EXPECT_CALL(*cds_cluster, initialize(_));

  create(parseBootstrapFromV3Json(json));

  ReadyWatcher cm_initialized;
  cluster_manager_->setInitializedCb([&]() -> void { cm_initialized.ready(); });

  const std::string ready_cluster_yaml = R"EOF(
    name: fake_cluster
    connect_timeout: 0.250s
    type: STATIC
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: fake_cluster
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 11001
  )EOF";

  const std::string warming_cluster_yaml = R"EOF(
    name: fake_cluster
    connect_timeout: 0.250s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: fake_cluster
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: foo.com
                port_value: 11001
  )EOF";

  {
    SCOPED_TRACE("Add a primary cluster staying in warming.");
    EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _));
    EXPECT_TRUE(cluster_manager_->addOrUpdateCluster(parseClusterFromV3Yaml(warming_cluster_yaml),
                                                     "warming"));

    // Mark all the rest of the clusters ready. Now the only warming cluster is the above one.
    EXPECT_CALL(cm_initialized, ready()).Times(0);
    cds_cluster->initialize_callback_();
  }

  {
    SCOPED_TRACE("Modify the only warming primary cluster to immediate ready.");
    EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _));
    EXPECT_CALL(*cds, initialize());
    EXPECT_TRUE(
        cluster_manager_->addOrUpdateCluster(parseClusterFromV3Yaml(ready_cluster_yaml), "ready"));
  }
  {
    SCOPED_TRACE("All clusters are ready.");
    EXPECT_CALL(cm_initialized, ready());
    cds->initialized_callback_();
  }
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cds_cluster.get()));
}

TEST_F(ClusterManagerImplTest, ModifyWarmingCluster) {
  time_system_.setSystemTime(std::chrono::milliseconds(1234567891234));
  create(defaultConfig());

  InSequence s;
  ReadyWatcher initialized;
  EXPECT_CALL(initialized, ready());
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

  // Add a "fake_cluster" in warming state.
  std::shared_ptr<MockClusterMockPrioritySet> cluster1 =
      std::make_shared<NiceMock<MockClusterMockPrioritySet>>();
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _))
      .WillOnce(Return(std::make_pair(cluster1, nullptr)));
  EXPECT_CALL(*cluster1, initializePhase()).Times(0);
  EXPECT_CALL(*cluster1, initialize(_));
  EXPECT_TRUE(
      cluster_manager_->addOrUpdateCluster(defaultStaticCluster("fake_cluster"), "version3"));
  checkStats(1 /*added*/, 0 /*modified*/, 0 /*removed*/, 0 /*active*/, 1 /*warming*/);
  EXPECT_EQ(nullptr, cluster_manager_->getThreadLocalCluster("fake_cluster"));
  checkConfigDump(R"EOF(
 dynamic_warming_clusters:
   - version_info: "version3"
     cluster:
       "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
       name: "fake_cluster"
       type: STATIC
       connect_timeout: 0.25s
       load_assignment:
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
     last_updated:
       seconds: 1234567891
       nanos: 234000000
 )EOF");

  // Update the warming cluster that was just added.
  std::shared_ptr<MockClusterMockPrioritySet> cluster2 =
      std::make_shared<NiceMock<MockClusterMockPrioritySet>>();
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _))
      .WillOnce(Return(std::make_pair(cluster2, nullptr)));
  EXPECT_CALL(*cluster2, initializePhase()).Times(0);
  EXPECT_CALL(*cluster2, initialize(_));
  EXPECT_TRUE(cluster_manager_->addOrUpdateCluster(
      parseClusterFromV3Json(fmt::sprintf(kDefaultStaticClusterTmpl, "fake_cluster",
                                          R"EOF(
"socket_address": {
  "address": "127.0.0.1",
  "port_value": 11002
})EOF")),
      "version3"));
  checkStats(1 /*added*/, 1 /*modified*/, 0 /*removed*/, 0 /*active*/, 1 /*warming*/);
  checkConfigDump(R"EOF(
 dynamic_warming_clusters:
   - version_info: "version3"
     cluster:
       "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
       name: "fake_cluster"
       type: STATIC
       connect_timeout: 0.25s
       load_assignment:
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11002
     last_updated:
       seconds: 1234567891
       nanos: 234000000
 )EOF");

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster1.get()));
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster2.get()));
}

// Regression test for https://github.com/envoyproxy/envoy/issues/14598.
// Make sure the revert isn't blocked due to being the same as the active version.
TEST_F(ClusterManagerImplTest, TestRevertWarmingCluster) {
  time_system_.setSystemTime(std::chrono::milliseconds(1234567891234));
  create(defaultConfig());

  InSequence s;
  ReadyWatcher initialized;
  EXPECT_CALL(initialized, ready());
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

  const std::string cluster_json1 = defaultStaticClusterJson("cds_cluster");
  const std::string cluster_json2 = fmt::sprintf(kDefaultStaticClusterTmpl, "cds_cluster",
                                                 R"EOF(
"socket_address": {
  "address": "127.0.0.1",
  "port_value": 11002
})EOF");

  std::shared_ptr<MockClusterMockPrioritySet> cluster1(new NiceMock<MockClusterMockPrioritySet>());
  std::shared_ptr<MockClusterMockPrioritySet> cluster2(new NiceMock<MockClusterMockPrioritySet>());
  std::shared_ptr<MockClusterMockPrioritySet> cluster3(new NiceMock<MockClusterMockPrioritySet>());
  cluster1->info_->name_ = "cds_cluster";
  cluster2->info_->name_ = "cds_cluster";
  cluster3->info_->name_ = "cds_cluster";

  // Initialize version1.
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _))
      .WillOnce(Return(std::make_pair(cluster1, nullptr)));
  EXPECT_CALL(*cluster1, initialize(_));
  checkStats(0 /*added*/, 0 /*modified*/, 0 /*removed*/, 0 /*active*/, 0 /*warming*/);

  cluster_manager_->addOrUpdateCluster(parseClusterFromV3Json(cluster_json1), "version1");
  checkStats(1 /*added*/, 0 /*modified*/, 0 /*removed*/, 0 /*active*/, 1 /*warming*/);

  cluster1->initialize_callback_();
  checkStats(1 /*added*/, 0 /*modified*/, 0 /*removed*/, 1 /*active*/, 0 /*warming*/);

  // Start warming version2.
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _))
      .WillOnce(Return(std::make_pair(cluster2, nullptr)));
  EXPECT_CALL(*cluster2, initialize(_));
  cluster_manager_->addOrUpdateCluster(parseClusterFromV3Json(cluster_json2), "version2");
  checkStats(1 /*added*/, 1 /*modified*/, 0 /*removed*/, 1 /*active*/, 1 /*warming*/);

  // Start warming version3 instead, which is the same as version1.
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _))
      .WillOnce(Return(std::make_pair(cluster3, nullptr)));
  EXPECT_CALL(*cluster3, initialize(_));
  cluster_manager_->addOrUpdateCluster(parseClusterFromV3Json(cluster_json1), "version3");
  checkStats(1 /*added*/, 2 /*modified*/, 0 /*removed*/, 1 /*active*/, 1 /*warming*/);

  // Finish warming version3.
  cluster3->initialize_callback_();
  checkStats(1 /*added*/, 2 /*modified*/, 0 /*removed*/, 1 /*active*/, 0 /*warming*/);
  checkConfigDump(R"EOF(
 dynamic_active_clusters:
   - version_info: "version3"
     cluster:
       "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
       name: "cds_cluster"
       type: STATIC
       connect_timeout: 0.25s
       load_assignment:
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
     last_updated:
       seconds: 1234567891
       nanos: 234000000
 )EOF");

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster1.get()));
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster2.get()));
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster3.get()));
}

// Verify that shutting down the cluster manager destroys warming clusters.
TEST_F(ClusterManagerImplTest, ShutdownWithWarming) {
  create(defaultConfig());

  InSequence s;
  ReadyWatcher initialized;
  EXPECT_CALL(initialized, ready());
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

  std::shared_ptr<MockClusterMockPrioritySet> cluster1(new NiceMock<MockClusterMockPrioritySet>());
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _))
      .WillOnce(Return(std::make_pair(cluster1, nullptr)));
  EXPECT_CALL(*cluster1, initializePhase()).Times(0);
  EXPECT_CALL(*cluster1, initialize(_));
  EXPECT_TRUE(
      cluster_manager_->addOrUpdateCluster(defaultStaticCluster("fake_cluster"), "version1"));
  checkStats(1 /*added*/, 0 /*modified*/, 0 /*removed*/, 0 /*active*/, 1 /*warming*/);
  cluster_manager_->shutdown();
  checkStats(1 /*added*/, 0 /*modified*/, 0 /*removed*/, 0 /*active*/, 0 /*warming*/);

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster1.get()));
}

TEST_F(ClusterManagerImplTest, DynamicAddRemove) {
  create(defaultConfig());

  InSequence s;
  ReadyWatcher initialized;
  EXPECT_CALL(initialized, ready());
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

  std::unique_ptr<MockClusterUpdateCallbacks> callbacks(new NiceMock<MockClusterUpdateCallbacks>());
  ClusterUpdateCallbacksHandlePtr cb =
      cluster_manager_->addThreadLocalClusterUpdateCallbacks(*callbacks);

  std::shared_ptr<MockClusterMockPrioritySet> cluster1(new NiceMock<MockClusterMockPrioritySet>());
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _))
      .WillOnce(Return(std::make_pair(cluster1, nullptr)));
  EXPECT_CALL(*cluster1, initializePhase()).Times(0);
  EXPECT_CALL(*cluster1, initialize(_));
  EXPECT_CALL(*callbacks, onClusterAddOrUpdate(_));
  EXPECT_TRUE(cluster_manager_->addOrUpdateCluster(defaultStaticCluster("fake_cluster"), ""));
  checkStats(1 /*added*/, 0 /*modified*/, 0 /*removed*/, 0 /*active*/, 1 /*warming*/);
  EXPECT_EQ(1, cluster_manager_->warmingClusterCount());
  EXPECT_EQ(nullptr, cluster_manager_->getThreadLocalCluster("fake_cluster"));
  cluster1->initialize_callback_();

  EXPECT_EQ(cluster1->info_, cluster_manager_->getThreadLocalCluster("fake_cluster")->info());
  checkStats(1 /*added*/, 0 /*modified*/, 0 /*removed*/, 1 /*active*/, 0 /*warming*/);
  EXPECT_EQ(0, cluster_manager_->warmingClusterCount());

  // Now try to update again but with the same hash.
  EXPECT_FALSE(cluster_manager_->addOrUpdateCluster(defaultStaticCluster("fake_cluster"), ""));

  // Now do it again with a different hash.
  auto update_cluster = defaultStaticCluster("fake_cluster");
  update_cluster.mutable_per_connection_buffer_limit_bytes()->set_value(12345);

  std::shared_ptr<MockClusterMockPrioritySet> cluster2(new NiceMock<MockClusterMockPrioritySet>());
  cluster2->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster2->info_, "tcp://127.0.0.1:80", time_system_)};
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _))
      .WillOnce(Return(std::make_pair(cluster2, nullptr)));
  EXPECT_CALL(*cluster2, initializePhase()).Times(0);
  EXPECT_CALL(*cluster2, initialize(_))
      .WillOnce(Invoke([cluster1](std::function<void()> initialize_callback) {
        // Test inline init.
        initialize_callback();
      }));
  EXPECT_CALL(*callbacks, onClusterAddOrUpdate(_));
  EXPECT_TRUE(cluster_manager_->addOrUpdateCluster(update_cluster, ""));

  EXPECT_EQ(cluster2->info_, cluster_manager_->getThreadLocalCluster("fake_cluster")->info());
  EXPECT_EQ(1UL, cluster_manager_->clusters().active_clusters_.size());
  Http::ConnectionPool::MockInstance* cp = new Http::ConnectionPool::MockInstance();
  Http::ConnectionPool::Instance::IdleCb idle_cb;
  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _)).WillOnce(Return(cp));
  EXPECT_CALL(*cp, addIdleCallback(_)).WillOnce(SaveArg<0>(&idle_cb));
  EXPECT_EQ(cp, HttpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("fake_cluster")
                                              ->httpConnPool(ResourcePriority::Default,
                                                             Http::Protocol::Http11, nullptr)));

  Tcp::ConnectionPool::MockInstance* cp2 = new Tcp::ConnectionPool::MockInstance();
  Tcp::ConnectionPool::Instance::IdleCb idle_cb2;
  EXPECT_CALL(factory_, allocateTcpConnPool_(_)).WillOnce(Return(cp2));
  EXPECT_CALL(*cp2, addIdleCallback(_)).WillOnce(SaveArg<0>(&idle_cb2));
  EXPECT_EQ(cp2, TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("fake_cluster")
                                              ->tcpConnPool(ResourcePriority::Default, nullptr)));

  Network::MockClientConnection* connection = new Network::MockClientConnection();
  ON_CALL(*cluster2->info_, features())
      .WillByDefault(Return(ClusterInfo::Features::CLOSE_CONNECTIONS_ON_HOST_HEALTH_FAILURE));
  EXPECT_CALL(factory_.tls_.dispatcher_, createClientConnection_(_, _, _, _))
      .WillOnce(Return(connection));
  EXPECT_CALL(*connection, setBufferLimits(_));
  EXPECT_CALL(*connection, addConnectionCallbacks(_));
  auto conn_info = cluster_manager_->getThreadLocalCluster("fake_cluster")->tcpConn(nullptr);
  EXPECT_EQ(conn_info.connection_.get(), connection);

  // Now remove the cluster. This should drain the connection pools, but not affect
  // tcp connections.
  EXPECT_CALL(*callbacks, onClusterRemoval(_));
  EXPECT_CALL(*cp, startDrain());
  EXPECT_CALL(*cp2, startDrain());
  EXPECT_TRUE(cluster_manager_->removeCluster("fake_cluster"));
  EXPECT_EQ(nullptr, cluster_manager_->getThreadLocalCluster("fake_cluster"));
  EXPECT_EQ(0UL, cluster_manager_->clusters().active_clusters_.size());

  // Close the TCP connection. Success is no ASSERT or crash due to referencing
  // the removed cluster.
  EXPECT_CALL(*connection, dispatcher());
  connection->raiseEvent(Network::ConnectionEvent::LocalClose);

  // Remove an unknown cluster.
  EXPECT_FALSE(cluster_manager_->removeCluster("foo"));

  idle_cb();
  idle_cb2();

  checkStats(1 /*added*/, 1 /*modified*/, 1 /*removed*/, 0 /*active*/, 0 /*warming*/);

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster1.get()));
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster2.get()));
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(callbacks.get()));
}

TEST_F(ClusterManagerImplTest, AddOrUpdateClusterStaticExists) {
  const std::string json = fmt::sprintf("{\"static_resources\":{%s}}",
                                        clustersJson({defaultStaticClusterJson("fake_cluster")}));
  std::shared_ptr<MockClusterMockPrioritySet> cluster1(new NiceMock<MockClusterMockPrioritySet>());
  InSequence s;
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _))
      .WillOnce(Return(std::make_pair(cluster1, nullptr)));
  ON_CALL(*cluster1, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(*cluster1, initialize(_));

  create(parseBootstrapFromV3Json(json));

  ReadyWatcher initialized;
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

  EXPECT_CALL(initialized, ready());
  cluster1->initialize_callback_();

  EXPECT_FALSE(cluster_manager_->addOrUpdateCluster(defaultStaticCluster("fake_cluster"), ""));

  // Attempt to remove a static cluster.
  EXPECT_FALSE(cluster_manager_->removeCluster("fake_cluster"));

  factory_.tls_.shutdownThread();

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster1.get()));
}

// Verifies that we correctly propagate the host_set state to the TLS clusters.
TEST_F(ClusterManagerImplTest, HostsPostedToTlsCluster) {
  const std::string json = fmt::sprintf("{\"static_resources\":{%s}}",
                                        clustersJson({defaultStaticClusterJson("fake_cluster")}));
  std::shared_ptr<MockClusterRealPrioritySet> cluster1(new NiceMock<MockClusterRealPrioritySet>());
  InSequence s;
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _))
      .WillOnce(Return(std::make_pair(cluster1, nullptr)));
  ON_CALL(*cluster1, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(*cluster1, initialize(_));

  create(parseBootstrapFromV3Json(json));

  ReadyWatcher initialized;
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

  EXPECT_CALL(initialized, ready());
  cluster1->initialize_callback_();

  // Set up the HostSet with 1 healthy, 1 degraded and 1 unhealthy.
  HostSharedPtr host1 = makeTestHost(cluster1->info_, "tcp://127.0.0.1:80", time_system_);
  host1->healthFlagSet(HostImpl::HealthFlag::DEGRADED_ACTIVE_HC);
  HostSharedPtr host2 = makeTestHost(cluster1->info_, "tcp://127.0.0.1:80", time_system_);
  host2->healthFlagSet(HostImpl::HealthFlag::FAILED_ACTIVE_HC);
  HostSharedPtr host3 = makeTestHost(cluster1->info_, "tcp://127.0.0.1:80", time_system_);

  HostVector hosts{host1, host2, host3};
  auto hosts_ptr = std::make_shared<HostVector>(hosts);

  cluster1->priority_set_.updateHosts(
      0, HostSetImpl::partitionHosts(hosts_ptr, HostsPerLocalityImpl::empty()), nullptr, hosts, {},
      100);

  auto* tls_cluster = cluster_manager_->getThreadLocalCluster(cluster1->info_->name());

  EXPECT_EQ(1, tls_cluster->prioritySet().hostSetsPerPriority().size());
  EXPECT_EQ(1, tls_cluster->prioritySet().hostSetsPerPriority()[0]->degradedHosts().size());
  EXPECT_EQ(host1, tls_cluster->prioritySet().hostSetsPerPriority()[0]->degradedHosts()[0]);
  EXPECT_EQ(1, tls_cluster->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(host3, tls_cluster->prioritySet().hostSetsPerPriority()[0]->healthyHosts()[0]);
  EXPECT_EQ(3, tls_cluster->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(100, tls_cluster->prioritySet().hostSetsPerPriority()[0]->overprovisioningFactor());

  factory_.tls_.shutdownThread();

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster1.get()));
}

// Test that we close all HTTP connection pool connections when there is a host health failure.
TEST_F(ClusterManagerImplTest, CloseHttpConnectionsOnHealthFailure) {
  const std::string json = fmt::sprintf("{\"static_resources\":{%s}}",
                                        clustersJson({defaultStaticClusterJson("some_cluster")}));
  std::shared_ptr<MockClusterMockPrioritySet> cluster1(new NiceMock<MockClusterMockPrioritySet>());
  cluster1->info_->name_ = "some_cluster";
  HostSharedPtr test_host = makeTestHost(cluster1->info_, "tcp://127.0.0.1:80", time_system_);
  cluster1->prioritySet().getMockHostSet(0)->hosts_ = {test_host};
  ON_CALL(*cluster1, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));

  MockHealthChecker health_checker;
  ON_CALL(*cluster1, healthChecker()).WillByDefault(Return(&health_checker));

  Outlier::MockDetector outlier_detector;
  ON_CALL(*cluster1, outlierDetector()).WillByDefault(Return(&outlier_detector));

  Http::ConnectionPool::MockInstance* cp1 = new NiceMock<Http::ConnectionPool::MockInstance>();
  Http::ConnectionPool::MockInstance* cp2 = new NiceMock<Http::ConnectionPool::MockInstance>();

  {
    InSequence s;

    EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _))
        .WillOnce(Return(std::make_pair(cluster1, nullptr)));
    EXPECT_CALL(health_checker, addHostCheckCompleteCb(_));
    EXPECT_CALL(outlier_detector, addChangedStateCb(_));
    EXPECT_CALL(*cluster1, initialize(_))
        .WillOnce(Invoke([cluster1](std::function<void()> initialize_callback) {
          // Test inline init.
          initialize_callback();
        }));
    create(parseBootstrapFromV3Json(json));

    EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _)).WillOnce(Return(cp1));
    cluster_manager_->getThreadLocalCluster("some_cluster")
        ->httpConnPool(ResourcePriority::Default, Http::Protocol::Http11, nullptr);

    outlier_detector.runCallbacks(test_host);
    health_checker.runCallbacks(test_host, HealthTransition::Unchanged);

    EXPECT_CALL(*cp1, drainConnections());
    test_host->healthFlagSet(Host::HealthFlag::FAILED_OUTLIER_CHECK);
    outlier_detector.runCallbacks(test_host);

    EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _)).WillOnce(Return(cp2));
    cluster_manager_->getThreadLocalCluster("some_cluster")
        ->httpConnPool(ResourcePriority::High, Http::Protocol::Http11, nullptr);
  }

  // Order of these calls is implementation dependent, so can't sequence them!
  EXPECT_CALL(*cp1, drainConnections());
  EXPECT_CALL(*cp2, drainConnections());
  test_host->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker.runCallbacks(test_host, HealthTransition::Changed);

  test_host->healthFlagClear(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  outlier_detector.runCallbacks(test_host);
  test_host->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker.runCallbacks(test_host, HealthTransition::Changed);

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster1.get()));
}

// Test that we close all HTTP connection pool connections when there is a host health failure.
// Verify that the pool gets deleted if it is idle, and that a crash does not occur due to
// deleting a container while iterating through it (see `do_not_delete_` in
// `ClusterManagerImpl::ThreadLocalClusterManagerImpl::onHostHealthFailure()`).
TEST_F(ClusterManagerImplTest, CloseHttpConnectionsAndDeletePoolOnHealthFailure) {
  const std::string json = fmt::sprintf("{\"static_resources\":{%s}}",
                                        clustersJson({defaultStaticClusterJson("some_cluster")}));
  std::shared_ptr<MockClusterMockPrioritySet> cluster1(new NiceMock<MockClusterMockPrioritySet>());
  cluster1->info_->name_ = "some_cluster";
  HostSharedPtr test_host = makeTestHost(cluster1->info_, "tcp://127.0.0.1:80", time_system_);
  cluster1->prioritySet().getMockHostSet(0)->hosts_ = {test_host};
  ON_CALL(*cluster1, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));

  MockHealthChecker health_checker;
  ON_CALL(*cluster1, healthChecker()).WillByDefault(Return(&health_checker));

  Outlier::MockDetector outlier_detector;
  ON_CALL(*cluster1, outlierDetector()).WillByDefault(Return(&outlier_detector));

  Http::ConnectionPool::MockInstance* cp1 = new NiceMock<Http::ConnectionPool::MockInstance>();

  InSequence s;

  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _))
      .WillOnce(Return(std::make_pair(cluster1, nullptr)));
  EXPECT_CALL(health_checker, addHostCheckCompleteCb(_));
  EXPECT_CALL(outlier_detector, addChangedStateCb(_));
  EXPECT_CALL(*cluster1, initialize(_))
      .WillOnce(Invoke([cluster1](std::function<void()> initialize_callback) {
        // Test inline init.
        initialize_callback();
      }));
  create(parseBootstrapFromV3Json(json));

  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _)).WillOnce(Return(cp1));
  cluster_manager_->getThreadLocalCluster("some_cluster")
      ->httpConnPool(ResourcePriority::Default, Http::Protocol::Http11, nullptr);

  outlier_detector.runCallbacks(test_host);
  health_checker.runCallbacks(test_host, HealthTransition::Unchanged);

  EXPECT_CALL(*cp1, drainConnections()).WillOnce(Invoke([&]() { cp1->idle_cb_(); }));
  test_host->healthFlagSet(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  outlier_detector.runCallbacks(test_host);

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster1.get()));
}

// Test that we close all TCP connection pool connections when there is a host health failure.
TEST_F(ClusterManagerImplTest, CloseTcpConnectionPoolsOnHealthFailure) {
  const std::string json = fmt::sprintf("{\"static_resources\":{%s}}",
                                        clustersJson({defaultStaticClusterJson("some_cluster")}));
  std::shared_ptr<MockClusterMockPrioritySet> cluster1(new NiceMock<MockClusterMockPrioritySet>());
  cluster1->info_->name_ = "some_cluster";
  HostSharedPtr test_host = makeTestHost(cluster1->info_, "tcp://127.0.0.1:80", time_system_);
  cluster1->prioritySet().getMockHostSet(0)->hosts_ = {test_host};
  ON_CALL(*cluster1, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));

  MockHealthChecker health_checker;
  ON_CALL(*cluster1, healthChecker()).WillByDefault(Return(&health_checker));

  Outlier::MockDetector outlier_detector;
  ON_CALL(*cluster1, outlierDetector()).WillByDefault(Return(&outlier_detector));

  Tcp::ConnectionPool::MockInstance* cp1 = new NiceMock<Tcp::ConnectionPool::MockInstance>();
  Tcp::ConnectionPool::MockInstance* cp2 = new NiceMock<Tcp::ConnectionPool::MockInstance>();

  {
    InSequence s;

    EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _))
        .WillOnce(Return(std::make_pair(cluster1, nullptr)));
    EXPECT_CALL(health_checker, addHostCheckCompleteCb(_));
    EXPECT_CALL(outlier_detector, addChangedStateCb(_));
    EXPECT_CALL(*cluster1, initialize(_))
        .WillOnce(Invoke([cluster1](std::function<void()> initialize_callback) {
          // Test inline init.
          initialize_callback();
        }));
    create(parseBootstrapFromV3Json(json));

    EXPECT_CALL(factory_, allocateTcpConnPool_(_)).WillOnce(Return(cp1));
    cluster_manager_->getThreadLocalCluster("some_cluster")
        ->tcpConnPool(ResourcePriority::Default, nullptr);

    outlier_detector.runCallbacks(test_host);
    health_checker.runCallbacks(test_host, HealthTransition::Unchanged);

    EXPECT_CALL(*cp1, drainConnections());
    test_host->healthFlagSet(Host::HealthFlag::FAILED_OUTLIER_CHECK);
    outlier_detector.runCallbacks(test_host);

    EXPECT_CALL(factory_, allocateTcpConnPool_(_)).WillOnce(Return(cp2));
    cluster_manager_->getThreadLocalCluster("some_cluster")
        ->tcpConnPool(ResourcePriority::High, nullptr);
  }

  // Order of these calls is implementation dependent, so can't sequence them!
  EXPECT_CALL(*cp1, drainConnections());
  EXPECT_CALL(*cp2, drainConnections());
  test_host->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker.runCallbacks(test_host, HealthTransition::Changed);

  test_host->healthFlagClear(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  outlier_detector.runCallbacks(test_host);
  test_host->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker.runCallbacks(test_host, HealthTransition::Changed);

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster1.get()));
}

// Test that we close all TCP connection pool connections when there is a host health failure,
// when configured to do so.
TEST_F(ClusterManagerImplTest, CloseTcpConnectionsOnHealthFailure) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: some_cluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      close_connections_on_host_health_failure: true
  )EOF";
  std::shared_ptr<MockClusterMockPrioritySet> cluster1(new NiceMock<MockClusterMockPrioritySet>());
  EXPECT_CALL(*cluster1->info_, features())
      .WillRepeatedly(Return(ClusterInfo::Features::CLOSE_CONNECTIONS_ON_HOST_HEALTH_FAILURE));
  cluster1->info_->name_ = "some_cluster";
  HostSharedPtr test_host = makeTestHost(cluster1->info_, "tcp://127.0.0.1:80", time_system_);
  cluster1->prioritySet().getMockHostSet(0)->hosts_ = {test_host};
  ON_CALL(*cluster1, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));

  MockHealthChecker health_checker;
  ON_CALL(*cluster1, healthChecker()).WillByDefault(Return(&health_checker));

  Outlier::MockDetector outlier_detector;
  ON_CALL(*cluster1, outlierDetector()).WillByDefault(Return(&outlier_detector));

  Network::MockClientConnection* connection1 = new NiceMock<Network::MockClientConnection>();
  Network::MockClientConnection* connection2 = new NiceMock<Network::MockClientConnection>();
  Host::CreateConnectionData conn_info1, conn_info2;

  {
    InSequence s;

    EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _))
        .WillOnce(Return(std::make_pair(cluster1, nullptr)));
    EXPECT_CALL(health_checker, addHostCheckCompleteCb(_));
    EXPECT_CALL(outlier_detector, addChangedStateCb(_));
    EXPECT_CALL(*cluster1, initialize(_))
        .WillOnce(Invoke([cluster1](std::function<void()> initialize_callback) {
          // Test inline init.
          initialize_callback();
        }));
    create(parseBootstrapFromV3Yaml(yaml));

    EXPECT_CALL(factory_.tls_.dispatcher_, createClientConnection_(_, _, _, _))
        .WillOnce(Return(connection1));
    conn_info1 = cluster_manager_->getThreadLocalCluster("some_cluster")->tcpConn(nullptr);

    outlier_detector.runCallbacks(test_host);
    health_checker.runCallbacks(test_host, HealthTransition::Unchanged);

    EXPECT_CALL(*connection1, close(Network::ConnectionCloseType::NoFlush));
    test_host->healthFlagSet(Host::HealthFlag::FAILED_OUTLIER_CHECK);
    outlier_detector.runCallbacks(test_host);

    connection1 = new NiceMock<Network::MockClientConnection>();
    EXPECT_CALL(factory_.tls_.dispatcher_, createClientConnection_(_, _, _, _))
        .WillOnce(Return(connection1));
    conn_info1 = cluster_manager_->getThreadLocalCluster("some_cluster")->tcpConn(nullptr);

    EXPECT_CALL(factory_.tls_.dispatcher_, createClientConnection_(_, _, _, _))
        .WillOnce(Return(connection2));
    conn_info2 = cluster_manager_->getThreadLocalCluster("some_cluster")->tcpConn(nullptr);
  }

  // Order of these calls is implementation dependent, so can't sequence them!
  EXPECT_CALL(*connection1, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connection2, close(Network::ConnectionCloseType::NoFlush));
  test_host->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker.runCallbacks(test_host, HealthTransition::Changed);

  test_host->healthFlagClear(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  outlier_detector.runCallbacks(test_host);
  test_host->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker.runCallbacks(test_host, HealthTransition::Changed);

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster1.get()));
}

// Test that we do not close TCP connection pool connections when there is a host health failure,
// when not configured to do so.
TEST_F(ClusterManagerImplTest, DoNotCloseTcpConnectionsOnHealthFailure) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: some_cluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      close_connections_on_host_health_failure: false
  )EOF";
  std::shared_ptr<MockClusterMockPrioritySet> cluster1(new NiceMock<MockClusterMockPrioritySet>());
  EXPECT_CALL(*cluster1->info_, features()).WillRepeatedly(Return(0));
  cluster1->info_->name_ = "some_cluster";
  HostSharedPtr test_host = makeTestHost(cluster1->info_, "tcp://127.0.0.1:80", time_system_);
  cluster1->prioritySet().getMockHostSet(0)->hosts_ = {test_host};
  ON_CALL(*cluster1, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));

  MockHealthChecker health_checker;
  ON_CALL(*cluster1, healthChecker()).WillByDefault(Return(&health_checker));

  Outlier::MockDetector outlier_detector;
  ON_CALL(*cluster1, outlierDetector()).WillByDefault(Return(&outlier_detector));

  Network::MockClientConnection* connection1 = new NiceMock<Network::MockClientConnection>();
  Host::CreateConnectionData conn_info1;

  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _))
      .WillOnce(Return(std::make_pair(cluster1, nullptr)));
  EXPECT_CALL(health_checker, addHostCheckCompleteCb(_));
  EXPECT_CALL(outlier_detector, addChangedStateCb(_));
  EXPECT_CALL(*cluster1, initialize(_))
      .WillOnce(Invoke([cluster1](std::function<void()> initialize_callback) {
        // Test inline init.
        initialize_callback();
      }));
  create(parseBootstrapFromV3Yaml(yaml));

  EXPECT_CALL(factory_.tls_.dispatcher_, createClientConnection_(_, _, _, _))
      .WillOnce(Return(connection1));
  conn_info1 = cluster_manager_->getThreadLocalCluster("some_cluster")->tcpConn(nullptr);

  outlier_detector.runCallbacks(test_host);
  health_checker.runCallbacks(test_host, HealthTransition::Unchanged);

  EXPECT_CALL(*connection1, close(_)).Times(0);
  test_host->healthFlagSet(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  outlier_detector.runCallbacks(test_host);

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster1.get()));
}

TEST_F(ClusterManagerImplTest, DynamicHostRemove) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.250s
      type: STRICT_DNS
      lb_policy: ROUND_ROBIN
      dns_resolution_config:
        resolvers:
          - socket_address:
              address: 1.2.3.4
              port_value: 80
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

  std::shared_ptr<Network::MockDnsResolver> dns_resolver(new Network::MockDnsResolver());
  EXPECT_CALL(factory_.dispatcher_, createDnsResolver(_, _)).WillOnce(Return(dns_resolver));

  Network::DnsResolver::ResolveCb dns_callback;
  Event::MockTimer* dns_timer_ = new NiceMock<Event::MockTimer>(&factory_.dispatcher_);
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));
  const auto all_clusters = cluster_manager_->clusters();
  EXPECT_TRUE(all_clusters.warming_clusters_.empty());
  EXPECT_EQ(all_clusters.active_clusters_.size(), 1);
  EXPECT_FALSE(all_clusters.active_clusters_.at("cluster_1").get().info()->addedViaApi());
  EXPECT_EQ(nullptr, cluster_manager_->getThreadLocalCluster("cluster_1"));

  // Set up for an initialize callback.
  ReadyWatcher initialized;
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });
  EXPECT_CALL(initialized, ready());

  dns_callback(Network::DnsResolver::ResolutionStatus::Success,
               TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2"}));

  // After we are initialized, we should immediately get called back if someone asks for an
  // initialize callback.
  EXPECT_CALL(initialized, ready());
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _))
      .Times(4)
      .WillRepeatedly(ReturnNew<NiceMock<Http::ConnectionPool::MockInstance>>());

  // This should provide us a CP for each of the above hosts.
  Http::ConnectionPool::MockInstance* cp1 = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(ResourcePriority::Default, Http::Protocol::Http11, nullptr));
  Http::ConnectionPool::MockInstance* cp2 = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(ResourcePriority::Default, Http::Protocol::Http11, nullptr));
  Http::ConnectionPool::MockInstance* cp1_high = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(ResourcePriority::High, Http::Protocol::Http11, nullptr));
  Http::ConnectionPool::MockInstance* cp2_high = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(ResourcePriority::High, Http::Protocol::Http11, nullptr));

  EXPECT_NE(cp1, cp2);
  EXPECT_NE(cp1_high, cp2_high);
  EXPECT_NE(cp1, cp1_high);

  EXPECT_CALL(factory_, allocateTcpConnPool_)
      .Times(4)
      .WillRepeatedly(ReturnNew<NiceMock<Tcp::ConnectionPool::MockInstance>>());

  // This should provide us a CP for each of the above hosts.
  Tcp::ConnectionPool::MockInstance* tcp1 =
      TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                   ->tcpConnPool(ResourcePriority::Default, nullptr));
  Tcp::ConnectionPool::MockInstance* tcp2 =
      TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                   ->tcpConnPool(ResourcePriority::Default, nullptr));
  Tcp::ConnectionPool::MockInstance* tcp1_high =
      TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                   ->tcpConnPool(ResourcePriority::High, nullptr));
  Tcp::ConnectionPool::MockInstance* tcp2_high =
      TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                   ->tcpConnPool(ResourcePriority::High, nullptr));

  EXPECT_NE(tcp1, tcp2);
  EXPECT_NE(tcp1_high, tcp2_high);
  EXPECT_NE(tcp1, tcp1_high);

  // Remove the first host, this should lead to the first cp being drained.
  dns_timer_->invokeCallback();
  dns_callback(Network::DnsResolver::ResolutionStatus::Success,
               TestUtility::makeDnsResponse({"127.0.0.2"}));
  cp1->idle_cb_();
  cp1->idle_cb_ = nullptr;
  tcp1->idle_cb_();
  tcp1->idle_cb_ = nullptr;
  EXPECT_CALL(factory_.tls_.dispatcher_, deferredDelete_(_)).Times(2);
  cp1_high->idle_cb_();
  cp1_high->idle_cb_ = nullptr;
  tcp1_high->idle_cb_();
  tcp1_high->idle_cb_ = nullptr;

  // Make sure we get back the same connection pool for the 2nd host as we did before the change.
  Http::ConnectionPool::MockInstance* cp3 = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(ResourcePriority::Default, Http::Protocol::Http11, nullptr));
  Http::ConnectionPool::MockInstance* cp3_high = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(ResourcePriority::High, Http::Protocol::Http11, nullptr));
  EXPECT_EQ(cp2, cp3);
  EXPECT_EQ(cp2_high, cp3_high);

  Tcp::ConnectionPool::MockInstance* tcp3 =
      TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                   ->tcpConnPool(ResourcePriority::Default, nullptr));
  Tcp::ConnectionPool::MockInstance* tcp3_high =
      TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                   ->tcpConnPool(ResourcePriority::High, nullptr));
  EXPECT_EQ(tcp2, tcp3);
  EXPECT_EQ(tcp2_high, tcp3_high);

  // Now add and remove a host that we never have a conn pool to. This should not lead to any
  // drain callbacks, etc.
  dns_timer_->invokeCallback();
  dns_callback(Network::DnsResolver::ResolutionStatus::Success,
               TestUtility::makeDnsResponse({"127.0.0.2", "127.0.0.3"}));
  factory_.tls_.shutdownThread();
}

TEST_F(ClusterManagerImplTest, DynamicHostRemoveWithTls) {
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
  )EOF";

  std::shared_ptr<Network::MockDnsResolver> dns_resolver(new Network::MockDnsResolver());
  EXPECT_CALL(factory_.dispatcher_, createDnsResolver(_, _)).WillOnce(Return(dns_resolver));

  Network::DnsResolver::ResolveCb dns_callback;
  Event::MockTimer* dns_timer_ = new NiceMock<Event::MockTimer>(&factory_.dispatcher_);
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));
  const auto all_clusters = cluster_manager_->clusters();
  EXPECT_TRUE(all_clusters.warming_clusters_.empty());
  EXPECT_EQ(all_clusters.active_clusters_.size(), 1);
  EXPECT_FALSE(all_clusters.active_clusters_.at("cluster_1").get().info()->addedViaApi());
  EXPECT_EQ(nullptr, cluster_manager_->getThreadLocalCluster("cluster_1"));

  NiceMock<MockLoadBalancerContext> example_com_context;
  ON_CALL(example_com_context, upstreamTransportSocketOptions())
      .WillByDefault(Return(std::make_shared<Network::TransportSocketOptionsImpl>("example.com")));

  NiceMock<MockLoadBalancerContext> example_com_context_with_san;
  ON_CALL(example_com_context_with_san, upstreamTransportSocketOptions())
      .WillByDefault(Return(std::make_shared<Network::TransportSocketOptionsImpl>(
          "example.com", std::vector<std::string>{"example.com"})));

  NiceMock<MockLoadBalancerContext> example_com_context_with_san2;
  ON_CALL(example_com_context_with_san2, upstreamTransportSocketOptions())
      .WillByDefault(Return(std::make_shared<Network::TransportSocketOptionsImpl>(
          "example.com", std::vector<std::string>{"example.net"})));

  NiceMock<MockLoadBalancerContext> ibm_com_context;
  ON_CALL(ibm_com_context, upstreamTransportSocketOptions())
      .WillByDefault(Return(std::make_shared<Network::TransportSocketOptionsImpl>("ibm.com")));

  // Set up for an initialize callback.
  ReadyWatcher initialized;
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });
  EXPECT_CALL(initialized, ready());

  dns_callback(Network::DnsResolver::ResolutionStatus::Success,
               TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2"}));

  // After we are initialized, we should immediately get called back if someone asks for an
  // initialize callback.
  EXPECT_CALL(initialized, ready());
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _))
      .Times(4)
      .WillRepeatedly(ReturnNew<NiceMock<Http::ConnectionPool::MockInstance>>());

  // This should provide us a CP for each of the above hosts.
  Http::ConnectionPool::MockInstance* cp1 = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(ResourcePriority::Default, Http::Protocol::Http11, nullptr));
  Http::ConnectionPool::MockInstance* cp2 = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(ResourcePriority::Default, Http::Protocol::Http11, nullptr));
  Http::ConnectionPool::MockInstance* cp1_high = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(ResourcePriority::High, Http::Protocol::Http11, nullptr));
  Http::ConnectionPool::MockInstance* cp2_high = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(ResourcePriority::High, Http::Protocol::Http11, nullptr));

  EXPECT_NE(cp1, cp2);
  EXPECT_NE(cp1_high, cp2_high);
  EXPECT_NE(cp1, cp1_high);

  EXPECT_CALL(factory_, allocateTcpConnPool_)
      .Times(10)
      .WillRepeatedly(ReturnNew<NiceMock<Tcp::ConnectionPool::MockInstance>>());

  // This should provide us a CP for each of the above hosts, and for different SNIs
  Tcp::ConnectionPool::MockInstance* tcp1 =
      TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                   ->tcpConnPool(ResourcePriority::Default, nullptr));
  Tcp::ConnectionPool::MockInstance* tcp2 =
      TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                   ->tcpConnPool(ResourcePriority::Default, nullptr));
  Tcp::ConnectionPool::MockInstance* tcp1_high =
      TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                   ->tcpConnPool(ResourcePriority::High, nullptr));
  Tcp::ConnectionPool::MockInstance* tcp2_high =
      TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                   ->tcpConnPool(ResourcePriority::High, nullptr));

  Tcp::ConnectionPool::MockInstance* tcp1_example_com =
      TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                   ->tcpConnPool(ResourcePriority::Default, &example_com_context));
  Tcp::ConnectionPool::MockInstance* tcp2_example_com =
      TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                   ->tcpConnPool(ResourcePriority::Default, &example_com_context));

  Tcp::ConnectionPool::MockInstance* tcp1_ibm_com =
      TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                   ->tcpConnPool(ResourcePriority::Default, &ibm_com_context));
  Tcp::ConnectionPool::MockInstance* tcp2_ibm_com =
      TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                   ->tcpConnPool(ResourcePriority::Default, &ibm_com_context));

  EXPECT_NE(tcp1, tcp2);
  EXPECT_NE(tcp1_high, tcp2_high);
  EXPECT_NE(tcp1, tcp1_high);

  EXPECT_NE(tcp1_ibm_com, tcp2_ibm_com);
  EXPECT_NE(tcp1_ibm_com, tcp1);
  EXPECT_NE(tcp1_ibm_com, tcp2);
  EXPECT_NE(tcp1_ibm_com, tcp1_high);
  EXPECT_NE(tcp1_ibm_com, tcp2_high);
  EXPECT_NE(tcp1_ibm_com, tcp1_example_com);
  EXPECT_NE(tcp1_ibm_com, tcp2_example_com);

  EXPECT_NE(tcp2_ibm_com, tcp1);
  EXPECT_NE(tcp2_ibm_com, tcp2);
  EXPECT_NE(tcp2_ibm_com, tcp1_high);
  EXPECT_NE(tcp2_ibm_com, tcp2_high);
  EXPECT_NE(tcp2_ibm_com, tcp1_example_com);
  EXPECT_NE(tcp2_ibm_com, tcp2_example_com);

  EXPECT_NE(tcp1_example_com, tcp1);
  EXPECT_NE(tcp1_example_com, tcp2);
  EXPECT_NE(tcp1_example_com, tcp1_high);
  EXPECT_NE(tcp1_example_com, tcp2_high);
  EXPECT_NE(tcp1_example_com, tcp2_example_com);

  EXPECT_NE(tcp2_example_com, tcp1);
  EXPECT_NE(tcp2_example_com, tcp2);
  EXPECT_NE(tcp2_example_com, tcp1_high);
  EXPECT_NE(tcp2_example_com, tcp2_high);

  EXPECT_CALL(factory_.tls_.dispatcher_, deferredDelete_(_)).Times(6);

  // Remove the first host, this should lead to the first cp being drained.
  dns_timer_->invokeCallback();
  dns_callback(Network::DnsResolver::ResolutionStatus::Success,
               TestUtility::makeDnsResponse({"127.0.0.2"}));
  cp1->idle_cb_();
  cp1->idle_cb_ = nullptr;
  tcp1->idle_cb_();
  tcp1->idle_cb_ = nullptr;
  cp1_high->idle_cb_();
  cp1_high->idle_cb_ = nullptr;
  tcp1_high->idle_cb_();
  tcp1_high->idle_cb_ = nullptr;
  tcp1_example_com->idle_cb_();
  tcp1_example_com->idle_cb_ = nullptr;
  tcp1_ibm_com->idle_cb_();
  tcp1_ibm_com->idle_cb_ = nullptr;

  // Make sure we get back the same connection pool for the 2nd host as we did before the change.
  Http::ConnectionPool::MockInstance* cp3 = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(ResourcePriority::Default, Http::Protocol::Http11, nullptr));
  Http::ConnectionPool::MockInstance* cp3_high = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(ResourcePriority::High, Http::Protocol::Http11, nullptr));
  EXPECT_EQ(cp2, cp3);
  EXPECT_EQ(cp2_high, cp3_high);

  Tcp::ConnectionPool::MockInstance* tcp3 =
      TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                   ->tcpConnPool(ResourcePriority::Default, nullptr));
  Tcp::ConnectionPool::MockInstance* tcp3_high =
      TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                   ->tcpConnPool(ResourcePriority::High, nullptr));

  Tcp::ConnectionPool::MockInstance* tcp3_example_com =
      TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                   ->tcpConnPool(ResourcePriority::Default, &example_com_context));
  Tcp::ConnectionPool::MockInstance* tcp3_example_com_with_san = TcpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->tcpConnPool(ResourcePriority::Default, &example_com_context_with_san));
  Tcp::ConnectionPool::MockInstance* tcp3_example_com_with_san2 = TcpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->tcpConnPool(ResourcePriority::Default, &example_com_context_with_san2));
  Tcp::ConnectionPool::MockInstance* tcp3_ibm_com =
      TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                   ->tcpConnPool(ResourcePriority::Default, &ibm_com_context));

  EXPECT_EQ(tcp2, tcp3);
  EXPECT_EQ(tcp2_high, tcp3_high);

  EXPECT_EQ(tcp2_example_com, tcp3_example_com);
  EXPECT_EQ(tcp2_ibm_com, tcp3_ibm_com);

  EXPECT_NE(tcp3_example_com, tcp3_example_com_with_san);
  EXPECT_NE(tcp3_example_com, tcp3_example_com_with_san2);
  EXPECT_NE(tcp3_example_com_with_san, tcp3_example_com_with_san2);

  // Now add and remove a host that we never have a conn pool to. This should not lead to any
  // drain callbacks, etc.
  dns_timer_->invokeCallback();
  dns_callback(Network::DnsResolver::ResolutionStatus::Success,
               TestUtility::makeDnsResponse({"127.0.0.2", "127.0.0.3"}));
  factory_.tls_.shutdownThread();
}

// Test that default DNS resolver with TCP lookups is used, when there are no DNS custom resolvers
// configured per cluster and `dns_resolver_options.use_tcp_for_dns_lookups` is set in bootstrap
// config.
TEST_F(ClusterManagerImplTest, UseTcpInDefaultDnsResolver) {
  const std::string yaml = R"EOF(
  dns_resolution_config:
    dns_resolver_options:
      use_tcp_for_dns_lookups: true
      no_default_search_domain: true
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.250s
      type: STRICT_DNS
  )EOF";

  std::shared_ptr<Network::MockDnsResolver> dns_resolver(new Network::MockDnsResolver());
  // As custom resolvers are not specified in config, this method should not be called,
  // resolver from context should be used instead.
  EXPECT_CALL(factory_.dispatcher_, createDnsResolver(_, _)).Times(0);

  Network::DnsResolver::ResolveCb dns_callback;
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));
  factory_.tls_.shutdownThread();
}

// Test that custom DNS resolver is used, when custom resolver is configured
// per cluster and deprecated field `dns_resolvers` is specified.
TEST_F(ClusterManagerImplTest, CustomDnsResolverSpecifiedViaDeprecatedField) {
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
  )EOF";

  std::shared_ptr<Network::MockDnsResolver> dns_resolver(new Network::MockDnsResolver());
  auto resolvers = envoy::config::core::v3::Address();
  resolvers.mutable_socket_address()->set_address("1.2.3.4");
  resolvers.mutable_socket_address()->set_port_value(80);
  std::vector<Network::Address::InstanceConstSharedPtr> expectedDnsResolvers;
  expectedDnsResolvers.push_back(Network::Address::resolveProtoAddress(resolvers));

  // As custom resolver is specified via deprecated field `dns_resolvers` in clusters
  // config, the method `createDnsResolver` is called once.
  EXPECT_CALL(factory_.dispatcher_,
              createDnsResolver(CustomDnsResolversSizeEquals(expectedDnsResolvers), _))
      .WillOnce(Return(dns_resolver));

  Network::DnsResolver::ResolveCb dns_callback;
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));
  factory_.tls_.shutdownThread();
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
  auto resolvers = envoy::config::core::v3::Address();
  resolvers.mutable_socket_address()->set_address("1.2.3.4");
  resolvers.mutable_socket_address()->set_port_value(80);
  std::vector<Network::Address::InstanceConstSharedPtr> expectedDnsResolvers;
  expectedDnsResolvers.push_back(Network::Address::resolveProtoAddress(resolvers));

  // As custom resolver is specified via field `dns_resolution_config.resolvers` in clusters
  // config, the method `createDnsResolver` is called once.
  EXPECT_CALL(factory_.dispatcher_,
              createDnsResolver(CustomDnsResolversSizeEquals(expectedDnsResolvers), _))
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
  envoy::config::core::v3::DnsResolverOptions dns_resolver_options;
  EXPECT_CALL(factory_.dispatcher_, createDnsResolver(_, _))
      .WillOnce(DoAll(SaveArg<1>(&dns_resolver_options), Return(dns_resolver)));

  Network::DnsResolver::ResolveCb dns_callback;
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));
  // `false` here means use_tcp_for_dns_lookups is not being set via bootstrap config
  EXPECT_EQ(false, dns_resolver_options.use_tcp_for_dns_lookups());
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
  envoy::config::core::v3::DnsResolverOptions dns_resolver_options;
  EXPECT_CALL(factory_.dispatcher_, createDnsResolver(_, _))
      .WillOnce(DoAll(SaveArg<1>(&dns_resolver_options), Return(dns_resolver)));

  Network::DnsResolver::ResolveCb dns_callback;
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));
  // `true` here means use_tcp_for_dns_lookups is set to true
  EXPECT_EQ(true, dns_resolver_options.use_tcp_for_dns_lookups());
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
  envoy::config::core::v3::DnsResolverOptions dns_resolver_options;
  EXPECT_CALL(factory_.dispatcher_, createDnsResolver(_, _))
      .WillOnce(DoAll(SaveArg<1>(&dns_resolver_options), Return(dns_resolver)));

  Network::DnsResolver::ResolveCb dns_callback;
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));
  // `false` here means dns_resolver_options.use_tcp_for_dns_lookups is set to false.
  EXPECT_EQ(false, dns_resolver_options.use_tcp_for_dns_lookups());
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
  envoy::config::core::v3::DnsResolverOptions dns_resolver_options;
  EXPECT_CALL(factory_.dispatcher_, createDnsResolver(_, _))
      .WillOnce(DoAll(SaveArg<1>(&dns_resolver_options), Return(dns_resolver)));

  Network::DnsResolver::ResolveCb dns_callback;
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));
  // `true` here means dns_resolver_options.use_tcp_for_dns_lookups is set to true.
  EXPECT_EQ(true, dns_resolver_options.use_tcp_for_dns_lookups());
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
  envoy::config::core::v3::DnsResolverOptions dns_resolver_options;
  EXPECT_CALL(factory_.dispatcher_, createDnsResolver(_, _))
      .WillOnce(DoAll(SaveArg<1>(&dns_resolver_options), Return(dns_resolver)));

  Network::DnsResolver::ResolveCb dns_callback;
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));
  // `true` here means dns_resolver_options.use_tcp_for_dns_lookups is set to true.
  EXPECT_EQ(true, dns_resolver_options.use_tcp_for_dns_lookups());
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
  envoy::config::core::v3::DnsResolverOptions dns_resolver_options;
  EXPECT_CALL(factory_.dispatcher_, createDnsResolver(_, _))
      .WillOnce(DoAll(SaveArg<1>(&dns_resolver_options), Return(dns_resolver)));

  Network::DnsResolver::ResolveCb dns_callback;
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));
  // `false` here means no_default_search_domain is not being set via bootstrap config
  EXPECT_EQ(false, dns_resolver_options.no_default_search_domain());
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
  envoy::config::core::v3::DnsResolverOptions dns_resolver_options;
  EXPECT_CALL(factory_.dispatcher_, createDnsResolver(_, _))
      .WillOnce(DoAll(SaveArg<1>(&dns_resolver_options), Return(dns_resolver)));

  Network::DnsResolver::ResolveCb dns_callback;
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));
  // `false` here means dns_resolver_options.no_default_search_domain is set to false.
  EXPECT_EQ(false, dns_resolver_options.no_default_search_domain());
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
  envoy::config::core::v3::DnsResolverOptions dns_resolver_options;
  EXPECT_CALL(factory_.dispatcher_, createDnsResolver(_, _))
      .WillOnce(DoAll(SaveArg<1>(&dns_resolver_options), Return(dns_resolver)));

  Network::DnsResolver::ResolveCb dns_callback;
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));
  // `true` here means dns_resolver_options.no_default_search_domain is set to true.
  EXPECT_EQ(true, dns_resolver_options.no_default_search_domain());
  factory_.tls_.shutdownThread();
}

// This is a regression test for a use-after-free in
// ClusterManagerImpl::ThreadLocalClusterManagerImpl::drainConnPools(), where a removal at one
// priority from the ConnPoolsContainer would delete the ConnPoolsContainer mid-iteration over the
// pool.
TEST_F(ClusterManagerImplTest, DynamicHostRemoveDefaultPriority) {
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
  )EOF";

  std::shared_ptr<Network::MockDnsResolver> dns_resolver(new Network::MockDnsResolver());
  EXPECT_CALL(factory_.dispatcher_, createDnsResolver(_, _)).WillOnce(Return(dns_resolver));

  Network::DnsResolver::ResolveCb dns_callback;
  Event::MockTimer* dns_timer_ = new NiceMock<Event::MockTimer>(&factory_.dispatcher_);
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));
  const auto all_clusters = cluster_manager_->clusters();
  EXPECT_TRUE(all_clusters.warming_clusters_.empty());
  EXPECT_EQ(all_clusters.active_clusters_.size(), 1);
  EXPECT_FALSE(all_clusters.active_clusters_.at("cluster_1").get().info()->addedViaApi());
  EXPECT_EQ(nullptr, cluster_manager_->getThreadLocalCluster("cluster_1"));

  dns_callback(Network::DnsResolver::ResolutionStatus::Success,
               TestUtility::makeDnsResponse({"127.0.0.2"}));

  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _))
      .WillOnce(ReturnNew<NiceMock<Http::ConnectionPool::MockInstance>>());

  EXPECT_CALL(factory_, allocateTcpConnPool_)
      .WillOnce(ReturnNew<NiceMock<Tcp::ConnectionPool::MockInstance>>());

  Http::ConnectionPool::MockInstance* cp = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(ResourcePriority::Default, Http::Protocol::Http11, nullptr));

  Tcp::ConnectionPool::MockInstance* tcp =
      TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                   ->tcpConnPool(ResourcePriority::Default, nullptr));

  // Immediate drain, since this can happen with the HTTP codecs.
  EXPECT_CALL(*cp, startDrain()).WillOnce(Invoke([&]() {
    cp->idle_cb_();
    cp->idle_cb_ = nullptr;
  }));
  EXPECT_CALL(*tcp, startDrain()).WillOnce(Invoke([&]() {
    tcp->idle_cb_();
    tcp->idle_cb_ = nullptr;
  }));

  // Remove the first host, this should lead to the cp being drained, without
  // crash.
  dns_timer_->invokeCallback();
  dns_callback(Network::DnsResolver::ResolutionStatus::Success, TestUtility::makeDnsResponse({}));

  factory_.tls_.shutdownThread();
}

class MockConnPoolWithDestroy : public Http::ConnectionPool::MockInstance {
public:
  ~MockConnPoolWithDestroy() override { onDestroy(); }

  MOCK_METHOD(void, onDestroy, ());
};

class MockTcpConnPoolWithDestroy : public Tcp::ConnectionPool::MockInstance {
public:
  ~MockTcpConnPoolWithDestroy() override { onDestroy(); }

  MOCK_METHOD(void, onDestroy, ());
};

// Regression test for https://github.com/envoyproxy/envoy/issues/3518. Make sure we handle a
// drain callback during CP destroy.
TEST_F(ClusterManagerImplTest, ConnPoolDestroyWithDraining) {
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
  )EOF";

  std::shared_ptr<Network::MockDnsResolver> dns_resolver(new Network::MockDnsResolver());
  EXPECT_CALL(factory_.dispatcher_, createDnsResolver(_, _)).WillOnce(Return(dns_resolver));

  Network::DnsResolver::ResolveCb dns_callback;
  Event::MockTimer* dns_timer_ = new NiceMock<Event::MockTimer>(&factory_.dispatcher_);
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));
  const auto all_clusters = cluster_manager_->clusters();
  EXPECT_TRUE(all_clusters.warming_clusters_.empty());
  EXPECT_EQ(all_clusters.active_clusters_.size(), 1);
  EXPECT_FALSE(all_clusters.active_clusters_.at("cluster_1").get().info()->addedViaApi());
  EXPECT_EQ(nullptr, cluster_manager_->getThreadLocalCluster("cluster_1"));

  dns_callback(Network::DnsResolver::ResolutionStatus::Success,
               TestUtility::makeDnsResponse({"127.0.0.2"}));

  MockConnPoolWithDestroy* mock_cp = new MockConnPoolWithDestroy();
  Http::ConnectionPool::Instance::IdleCb drained_cb;
  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _)).WillOnce(Return(mock_cp));
  EXPECT_CALL(*mock_cp, addIdleCallback(_)).WillOnce(SaveArg<0>(&drained_cb));
  EXPECT_CALL(*mock_cp, startDrain());

  MockTcpConnPoolWithDestroy* mock_tcp = new NiceMock<MockTcpConnPoolWithDestroy>();
  Tcp::ConnectionPool::Instance::IdleCb tcp_drained_cb;
  EXPECT_CALL(factory_, allocateTcpConnPool_).WillOnce(Return(mock_tcp));
  EXPECT_CALL(*mock_tcp, addIdleCallback(_)).WillOnce(SaveArg<0>(&tcp_drained_cb));
  EXPECT_CALL(*mock_tcp, startDrain());

  HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(ResourcePriority::Default, Http::Protocol::Http11, nullptr));

  TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                               ->tcpConnPool(ResourcePriority::Default, nullptr));

  // Remove the first host, this should lead to the cp being drained.
  dns_timer_->invokeCallback();
  dns_callback(Network::DnsResolver::ResolutionStatus::Success, TestUtility::makeDnsResponse({}));

  // The drained callback might get called when the CP is being destroyed.
  EXPECT_CALL(*mock_cp, onDestroy()).WillOnce(Invoke(drained_cb));
  EXPECT_CALL(*mock_tcp, onDestroy()).WillOnce(Invoke(tcp_drained_cb));
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
                ->httpConnPool(ResourcePriority::Default, Http::Protocol::Http11, nullptr));
  EXPECT_EQ(absl::nullopt, cluster_manager_->getThreadLocalCluster("cluster_1")
                               ->tcpConnPool(ResourcePriority::Default, nullptr));
  EXPECT_EQ(nullptr,
            cluster_manager_->getThreadLocalCluster("cluster_1")->tcpConn(nullptr).connection_);
  EXPECT_EQ(3UL, factory_.stats_.counter("cluster.cluster_1.upstream_cx_none_healthy").value());

  factory_.tls_.shutdownThread();
}

// Tests that all the HC/weight/metadata changes are delivered in one go, as long as
// there's no hosts changes in between.
// Also tests that if hosts are added/removed between mergeable updates, delivery will
// happen and the scheduled update will be cancelled.
TEST_F(ClusterManagerImplTest, MergedUpdates) {
  createWithLocalClusterUpdate();

  // Ensure we see the right set of added/removed hosts on every call.
  EXPECT_CALL(local_cluster_update_, post(_, _, _))
      .WillOnce(Invoke([](uint32_t priority, const HostVector& hosts_added,
                          const HostVector& hosts_removed) -> void {
        // 1st removal.
        EXPECT_EQ(0, priority);
        EXPECT_EQ(0, hosts_added.size());
        EXPECT_EQ(1, hosts_removed.size());
      }))
      .WillOnce(Invoke([](uint32_t priority, const HostVector& hosts_added,
                          const HostVector& hosts_removed) -> void {
        // Triggered by the 2 HC updates, it's a merged update so no added/removed
        // hosts.
        EXPECT_EQ(0, priority);
        EXPECT_EQ(0, hosts_added.size());
        EXPECT_EQ(0, hosts_removed.size());
      }))
      .WillOnce(Invoke([](uint32_t priority, const HostVector& hosts_added,
                          const HostVector& hosts_removed) -> void {
        // 1st removed host added back.
        EXPECT_EQ(0, priority);
        EXPECT_EQ(1, hosts_added.size());
        EXPECT_EQ(0, hosts_removed.size());
      }))
      .WillOnce(Invoke([](uint32_t priority, const HostVector& hosts_added,
                          const HostVector& hosts_removed) -> void {
        // 1st removed host removed again, plus the 3 HC/weight/metadata updates that were
        // waiting for delivery.
        EXPECT_EQ(0, priority);
        EXPECT_EQ(0, hosts_added.size());
        EXPECT_EQ(1, hosts_removed.size());
      }));

  EXPECT_CALL(local_hosts_removed_, post(_))
      .Times(2)
      .WillRepeatedly(
          Invoke([](const auto& hosts_removed) { EXPECT_EQ(1, hosts_removed.size()); }));

  Event::MockTimer* timer = new NiceMock<Event::MockTimer>(&factory_.dispatcher_);
  Cluster& cluster = cluster_manager_->activeClusters().begin()->second;
  HostVectorSharedPtr hosts(
      new HostVector(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()));
  HostsPerLocalitySharedPtr hosts_per_locality = std::make_shared<HostsPerLocalityImpl>();
  HostVector hosts_added;
  HostVector hosts_removed;

  // The first update should be applied immediately, since it's not mergeable.
  hosts_removed.push_back((*hosts)[0]);
  cluster.prioritySet().updateHosts(
      0,
      updateHostsParams(hosts, hosts_per_locality,
                        std::make_shared<const HealthyHostVector>(*hosts), hosts_per_locality),
      {}, hosts_added, hosts_removed, absl::nullopt);
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());

  // These calls should be merged, since there are no added/removed hosts.
  hosts_removed.clear();
  cluster.prioritySet().updateHosts(
      0,
      updateHostsParams(hosts, hosts_per_locality,
                        std::make_shared<const HealthyHostVector>(*hosts), hosts_per_locality),
      {}, hosts_added, hosts_removed, absl::nullopt);
  cluster.prioritySet().updateHosts(
      0,
      updateHostsParams(hosts, hosts_per_locality,
                        std::make_shared<const HealthyHostVector>(*hosts), hosts_per_locality),
      {}, hosts_added, hosts_removed, absl::nullopt);
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());

  // Ensure the merged updates were applied.
  timer->invokeCallback();
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());

  // Add the host back, the update should be immediately applied.
  hosts_removed.clear();
  hosts_added.push_back((*hosts)[0]);
  cluster.prioritySet().updateHosts(
      0,
      updateHostsParams(hosts, hosts_per_locality,
                        std::make_shared<const HealthyHostVector>(*hosts), hosts_per_locality),
      {}, hosts_added, hosts_removed, absl::nullopt);
  EXPECT_EQ(2, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());

  // Now emit 3 updates that should be scheduled: metadata, HC, and weight.
  hosts_added.clear();

  (*hosts)[0]->metadata(buildMetadata("v1"));
  cluster.prioritySet().updateHosts(
      0,
      updateHostsParams(hosts, hosts_per_locality,
                        std::make_shared<const HealthyHostVector>(*hosts), hosts_per_locality),
      {}, hosts_added, hosts_removed, absl::nullopt);

  (*hosts)[0]->healthFlagSet(Host::HealthFlag::FAILED_EDS_HEALTH);
  cluster.prioritySet().updateHosts(
      0,
      updateHostsParams(hosts, hosts_per_locality,
                        std::make_shared<const HealthyHostVector>(*hosts), hosts_per_locality),
      {}, hosts_added, hosts_removed, absl::nullopt);

  (*hosts)[0]->weight(100);
  cluster.prioritySet().updateHosts(
      0,
      updateHostsParams(hosts, hosts_per_locality,
                        std::make_shared<const HealthyHostVector>(*hosts), hosts_per_locality),
      {}, hosts_added, hosts_removed, absl::nullopt);

  // Updates not delivered yet.
  EXPECT_EQ(2, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());

  // Remove the host again, should cancel the scheduled update and be delivered immediately.
  hosts_removed.push_back((*hosts)[0]);
  cluster.prioritySet().updateHosts(
      0,
      updateHostsParams(hosts, hosts_per_locality,
                        std::make_shared<const HealthyHostVector>(*hosts), hosts_per_locality),
      {}, hosts_added, hosts_removed, absl::nullopt);

  EXPECT_EQ(3, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());
}

// Tests that mergeable updates outside of a window get applied immediately.
TEST_F(ClusterManagerImplTest, MergedUpdatesOutOfWindow) {
  createWithLocalClusterUpdate();

  // Ensure we see the right set of added/removed hosts on every call.
  EXPECT_CALL(local_cluster_update_, post(_, _, _))
      .WillOnce(Invoke([](uint32_t priority, const HostVector& hosts_added,
                          const HostVector& hosts_removed) -> void {
        // HC update, immediately delivered.
        EXPECT_EQ(0, priority);
        EXPECT_EQ(0, hosts_added.size());
        EXPECT_EQ(0, hosts_removed.size());
      }));

  Cluster& cluster = cluster_manager_->activeClusters().begin()->second;
  HostVectorSharedPtr hosts(
      new HostVector(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()));
  HostsPerLocalitySharedPtr hosts_per_locality = std::make_shared<HostsPerLocalityImpl>();
  HostVector hosts_added;
  HostVector hosts_removed;

  // The first update should be applied immediately, because even though it's mergeable
  // it's outside the default merge window of 3 seconds (found in debugger as value of
  // cluster.info()->lbConfig().update_merge_window() in ClusterManagerImpl::scheduleUpdate.
  time_system_.advanceTimeWait(std::chrono::seconds(60));
  cluster.prioritySet().updateHosts(
      0,
      updateHostsParams(hosts, hosts_per_locality,
                        std::make_shared<const HealthyHostVector>(*hosts), hosts_per_locality),
      {}, hosts_added, hosts_removed, absl::nullopt);
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.update_out_of_merge_window").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());
}

// Tests that mergeable updates inside of a window are not applied immediately.
TEST_F(ClusterManagerImplTest, MergedUpdatesInsideWindow) {
  createWithLocalClusterUpdate();

  Cluster& cluster = cluster_manager_->activeClusters().begin()->second;
  HostVectorSharedPtr hosts(
      new HostVector(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()));
  HostsPerLocalitySharedPtr hosts_per_locality = std::make_shared<HostsPerLocalityImpl>();
  HostVector hosts_added;
  HostVector hosts_removed;

  // The first update will not be applied, as we make it inside the default mergeable window of
  // 3 seconds (found in debugger as value of cluster.info()->lbConfig().update_merge_window()
  // in ClusterManagerImpl::scheduleUpdate. Note that initially the update-time is
  // default-initialized to a monotonic time of 0, as is SimulatedTimeSystem::monotonic_time_.
  time_system_.advanceTimeWait(std::chrono::seconds(2));
  cluster.prioritySet().updateHosts(
      0,
      updateHostsParams(hosts, hosts_per_locality,
                        std::make_shared<const HealthyHostVector>(*hosts), hosts_per_locality),
      {}, hosts_added, hosts_removed, absl::nullopt);
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_out_of_merge_window").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());
}

// Tests that mergeable updates outside of a window get applied immediately when
// merging is disabled, and that the counters are correct.
TEST_F(ClusterManagerImplTest, MergedUpdatesOutOfWindowDisabled) {
  createWithLocalClusterUpdate(false);

  // Ensure we see the right set of added/removed hosts on every call.
  EXPECT_CALL(local_cluster_update_, post(_, _, _))
      .WillOnce(Invoke([](uint32_t priority, const HostVector& hosts_added,
                          const HostVector& hosts_removed) -> void {
        // HC update, immediately delivered.
        EXPECT_EQ(0, priority);
        EXPECT_EQ(0, hosts_added.size());
        EXPECT_EQ(0, hosts_removed.size());
      }));

  Cluster& cluster = cluster_manager_->activeClusters().begin()->second;
  HostVectorSharedPtr hosts(
      new HostVector(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()));
  HostsPerLocalitySharedPtr hosts_per_locality = std::make_shared<HostsPerLocalityImpl>();
  HostVector hosts_added;
  HostVector hosts_removed;

  // The first update should be applied immediately, because even though it's mergeable
  // and outside a merge window, merging is disabled.
  cluster.prioritySet().updateHosts(
      0,
      updateHostsParams(hosts, hosts_per_locality,
                        std::make_shared<const HealthyHostVector>(*hosts), hosts_per_locality),
      {}, hosts_added, hosts_removed, absl::nullopt);
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_out_of_merge_window").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());
}

TEST_F(ClusterManagerImplTest, MergedUpdatesDestroyedOnUpdate) {
  // We create the default cluster, although for this test we won't use it since
  // we can only update dynamic clusters.
  createWithLocalClusterUpdate();

  // Ensure we see the right set of added/removed hosts on every call, for the
  // dynamically added/updated cluster.
  EXPECT_CALL(local_cluster_update_, post(_, _, _))
      .WillOnce(Invoke([](uint32_t priority, const HostVector& hosts_added,
                          const HostVector& hosts_removed) -> void {
        // 1st add, when the cluster is added.
        EXPECT_EQ(0, priority);
        EXPECT_EQ(1, hosts_added.size());
        EXPECT_EQ(0, hosts_removed.size());
      }))
      .WillOnce(Invoke([](uint32_t priority, const HostVector& hosts_added,
                          const HostVector& hosts_removed) -> void {
        // 1st removal.
        EXPECT_EQ(0, priority);
        EXPECT_EQ(0, hosts_added.size());
        EXPECT_EQ(1, hosts_removed.size());
      }));

  EXPECT_CALL(local_hosts_removed_, post(_)).WillOnce(Invoke([](const auto& hosts_removed) {
    // 1st removal.
    EXPECT_EQ(1, hosts_removed.size());
  }));

  Event::MockTimer* timer = new NiceMock<Event::MockTimer>(&factory_.dispatcher_);

  // We can't used the bootstrap cluster, so add one dynamically.
  const std::string yaml = R"EOF(
  name: new_cluster
  connect_timeout: 0.250s
  type: STATIC
  lb_policy: ROUND_ROBIN
  load_assignment:
    cluster_name: new_cluster
    endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 12001
  common_lb_config:
    update_merge_window: 3s
  )EOF";
  EXPECT_TRUE(cluster_manager_->addOrUpdateCluster(parseClusterFromV3Yaml(yaml), "version1"));

  Cluster& cluster = cluster_manager_->activeClusters().find("new_cluster")->second;
  HostVectorSharedPtr hosts(
      new HostVector(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()));
  HostsPerLocalitySharedPtr hosts_per_locality = std::make_shared<HostsPerLocalityImpl>();
  HostVector hosts_added;
  HostVector hosts_removed;

  // The first update should be applied immediately, since it's not mergeable.
  hosts_removed.push_back((*hosts)[0]);
  cluster.prioritySet().updateHosts(
      0,
      updateHostsParams(hosts, hosts_per_locality,
                        std::make_shared<const HealthyHostVector>(*hosts), hosts_per_locality),
      {}, hosts_added, hosts_removed, absl::nullopt);
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());

  // These calls should be merged, since there are no added/removed hosts.
  hosts_removed.clear();
  cluster.prioritySet().updateHosts(
      0,
      updateHostsParams(hosts, hosts_per_locality,
                        std::make_shared<const HealthyHostVector>(*hosts), hosts_per_locality),
      {}, hosts_added, hosts_removed, absl::nullopt);
  cluster.prioritySet().updateHosts(
      0,
      updateHostsParams(hosts, hosts_per_locality,
                        std::make_shared<const HealthyHostVector>(*hosts), hosts_per_locality),
      {}, hosts_added, hosts_removed, absl::nullopt);
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());

  // Update the cluster, which should cancel the pending updates.
  std::shared_ptr<MockClusterMockPrioritySet> updated(new NiceMock<MockClusterMockPrioritySet>());
  updated->info_->name_ = "new_cluster";
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, true))
      .WillOnce(Return(std::make_pair(updated, nullptr)));

  const std::string yaml_updated = R"EOF(
  name: new_cluster
  connect_timeout: 0.250s
  type: STATIC
  lb_policy: ROUND_ROBIN
  load_assignment:
    cluster_name: new_cluster
    endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 12001
  common_lb_config:
    update_merge_window: 4s
  )EOF";

  // Add the updated cluster.
  EXPECT_EQ(2, factory_.stats_
                   .gauge("cluster_manager.active_clusters", Stats::Gauge::ImportMode::NeverImport)
                   .value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_modified").value());
  EXPECT_EQ(0, factory_.stats_
                   .gauge("cluster_manager.warming_clusters", Stats::Gauge::ImportMode::NeverImport)
                   .value());
  EXPECT_TRUE(
      cluster_manager_->addOrUpdateCluster(parseClusterFromV3Yaml(yaml_updated), "version2"));
  EXPECT_EQ(2, factory_.stats_
                   .gauge("cluster_manager.active_clusters", Stats::Gauge::ImportMode::NeverImport)
                   .value());
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_modified").value());
  EXPECT_EQ(1, factory_.stats_
                   .gauge("cluster_manager.warming_clusters", Stats::Gauge::ImportMode::NeverImport)
                   .value());

  // Promote the updated cluster from warming to active & assert the old timer was disabled
  // and it won't be called on version1 of new_cluster.
  EXPECT_CALL(*timer, disableTimer());
  updated->initialize_callback_();

  EXPECT_EQ(2, factory_.stats_
                   .gauge("cluster_manager.active_clusters", Stats::Gauge::ImportMode::NeverImport)
                   .value());
  EXPECT_EQ(0, factory_.stats_
                   .gauge("cluster_manager.warming_clusters", Stats::Gauge::ImportMode::NeverImport)
                   .value());
}

TEST_F(ClusterManagerImplTest, UpstreamSocketOptionsPassedToTcpConnPool) {
  createWithLocalClusterUpdate();
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

TEST_F(ClusterManagerImplTest, UpstreamSocketOptionsPassedToConnPool) {
  createWithLocalClusterUpdate();
  NiceMock<MockLoadBalancerContext> context;

  Http::ConnectionPool::MockInstance* to_create =
      new NiceMock<Http::ConnectionPool::MockInstance>();
  Network::Socket::OptionsSharedPtr options_to_return =
      Network::SocketOptionFactory::buildIpTransparentOptions();

  EXPECT_CALL(context, upstreamSocketOptions()).WillOnce(Return(options_to_return));
  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _)).WillOnce(Return(to_create));

  auto opt_cp = cluster_manager_->getThreadLocalCluster("cluster_1")
                    ->httpConnPool(ResourcePriority::Default, Http::Protocol::Http11, &context);
  EXPECT_TRUE(opt_cp.has_value());
}

TEST_F(ClusterManagerImplTest, UpstreamSocketOptionsUsedInConnPoolHash) {
  createWithLocalClusterUpdate();
  NiceMock<MockLoadBalancerContext> context1;
  NiceMock<MockLoadBalancerContext> context2;

  Http::ConnectionPool::MockInstance* to_create1 =
      new NiceMock<Http::ConnectionPool::MockInstance>();
  Http::ConnectionPool::MockInstance* to_create2 =
      new NiceMock<Http::ConnectionPool::MockInstance>();
  Network::Socket::OptionsSharedPtr options1 =
      Network::SocketOptionFactory::buildIpTransparentOptions();
  Network::Socket::OptionsSharedPtr options2 =
      Network::SocketOptionFactory::buildSocketMarkOptions(3);

  EXPECT_CALL(context1, upstreamSocketOptions()).WillRepeatedly(Return(options1));
  EXPECT_CALL(context2, upstreamSocketOptions()).WillRepeatedly(Return(options2));
  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _)).WillOnce(Return(to_create1));

  Http::ConnectionPool::Instance* cp1 = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(ResourcePriority::Default, Http::Protocol::Http11, &context1));

  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _)).WillOnce(Return(to_create2));
  Http::ConnectionPool::Instance* cp2 = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(ResourcePriority::Default, Http::Protocol::Http11, &context2));

  Http::ConnectionPool::Instance* should_be_cp1 = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(ResourcePriority::Default, Http::Protocol::Http11, &context1));
  EXPECT_NE(nullptr, cp1);
  EXPECT_NE(nullptr, cp2);
  Http::ConnectionPool::Instance* should_be_cp2 = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(ResourcePriority::Default, Http::Protocol::Http11, &context2));

  // The different upstream options should lead to different hashKeys, thus different pools.
  EXPECT_NE(cp1, cp2);

  // Reusing the same options should lead to the same connection pools.
  EXPECT_EQ(cp1, should_be_cp1);
  EXPECT_EQ(cp2, should_be_cp2);
}

TEST_F(ClusterManagerImplTest, UpstreamSocketOptionsNullIsOkay) {
  createWithLocalClusterUpdate();
  NiceMock<MockLoadBalancerContext> context;

  Http::ConnectionPool::MockInstance* to_create =
      new NiceMock<Http::ConnectionPool::MockInstance>();
  Network::Socket::OptionsSharedPtr options_to_return = nullptr;

  EXPECT_CALL(context, upstreamSocketOptions()).WillOnce(Return(options_to_return));
  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _)).WillOnce(Return(to_create));

  auto opt_cp = cluster_manager_->getThreadLocalCluster("cluster_1")
                    ->httpConnPool(ResourcePriority::Default, Http::Protocol::Http11, &context);
  EXPECT_TRUE(opt_cp.has_value());
}

TEST_F(ClusterManagerImplTest, HttpPoolDataForwardsCallsToConnectionPool) {
  createWithLocalClusterUpdate();
  NiceMock<MockLoadBalancerContext> context;

  Http::ConnectionPool::MockInstance* pool_mock = new Http::ConnectionPool::MockInstance();
  Network::Socket::OptionsSharedPtr options_to_return = nullptr;

  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _)).WillOnce(Return(pool_mock));
  EXPECT_CALL(*pool_mock, addIdleCallback(_));

  auto opt_cp = cluster_manager_->getThreadLocalCluster("cluster_1")
                    ->httpConnPool(ResourcePriority::Default, Http::Protocol::Http11, &context);
  ASSERT_TRUE(opt_cp.has_value());

  EXPECT_CALL(*pool_mock, hasActiveConnections()).WillOnce(Return(true));
  opt_cp.value().hasActiveConnections();

  ConnectionPool::Instance::IdleCb drained_cb = []() {};
  EXPECT_CALL(*pool_mock, addIdleCallback(_));
  opt_cp.value().addIdleCallback(drained_cb);
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
                               Server::Configuration::CommonFactoryContext&) override {
    return [](Network::FilterManager& filter_manager) -> void {
      filter_manager.addWriteFilter(std::make_shared<TestUpstreamNetworkFilter>());
    };
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Struct instead of a custom per-filter empty config proto
    // This is only allowed in tests.
    return std::make_unique<Envoy::ProtobufWkt::Struct>();
  }
  std::string name() const override { return "envoy.test.filter"; }
};

// Verify that configured upstream filters are added to client connections.
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

  NiceMock<MockClusterManager> cm_;
  ClusterManagerInitHelper init_helper_{
      cm_, [this](ClusterManagerCluster& cluster) { onClusterInit(cluster); }};
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

  NiceMock<MockClusterMockPrioritySet> cluster_;
  bool added_or_updated_{};
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

using NameVals = std::vector<std::pair<Network::SocketOptionName, int>>;

// Validate that when options are set in the ClusterManager and/or Cluster, we see the socket option
// propagated to setsockopt(). This is as close to an end-to-end test as we have for this feature,
// due to the complexity of creating an integration test involving the network stack. We only test
// the IPv4 case here, as the logic around IPv4/IPv6 handling is tested generically in
// socket_option_impl_test.cc.
class SockoptsTest : public ClusterManagerImplTest {
public:
  void initialize(const std::string& yaml) { create(parseBootstrapFromV3Yaml(yaml)); }

  void TearDown() override { factory_.tls_.shutdownThread(); }

  // TODO(tschroed): Extend this to support socket state as well.
  void expectSetsockopts(const NameVals& names_vals) {
    NiceMock<Api::MockOsSysCalls> os_sys_calls;
    TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
    NiceMock<Network::MockConnectionSocket> socket;
    bool expect_success = true;
    for (const auto& name_val : names_vals) {
      if (!name_val.first.hasValue()) {
        expect_success = false;
        continue;
      }
      EXPECT_CALL(socket,
                  setSocketOption(name_val.first.level(), name_val.first.option(), _, sizeof(int)))
          .WillOnce(
              Invoke([&name_val](int, int, const void* optval, socklen_t) -> Api::SysCallIntResult {
                EXPECT_EQ(name_val.second, *static_cast<const int*>(optval));
                return {0, 0};
              }));
    }
    EXPECT_CALL(socket, ipVersion())
        .WillRepeatedly(testing::Return(Network::Address::IpVersion::v4));
    EXPECT_CALL(factory_.tls_.dispatcher_, createClientConnection_(_, _, _, _))
        .WillOnce(Invoke([this, &names_vals, expect_success, &socket](
                             Network::Address::InstanceConstSharedPtr,
                             Network::Address::InstanceConstSharedPtr, Network::TransportSocketPtr&,
                             const Network::ConnectionSocket::OptionsSharedPtr& options)
                             -> Network::ClientConnection* {
          EXPECT_NE(nullptr, options.get()) << "Unexpected null options";
          if (options.get() != nullptr) { // Don't crash the entire test.
            EXPECT_EQ(names_vals.size(), options->size());
          }
          if (expect_success) {
            EXPECT_TRUE((Network::Socket::applyOptions(
                options, socket, envoy::config::core::v3::SocketOption::STATE_PREBIND)));
          } else {
            EXPECT_FALSE((Network::Socket::applyOptions(
                options, socket, envoy::config::core::v3::SocketOption::STATE_PREBIND)));
          }
          return connection_;
        }));
    cluster_manager_->getThreadLocalCluster("SockoptsCluster")->tcpConn(nullptr);
  }

  void expectSetsockoptFreebind() {
    NameVals names_vals{{ENVOY_SOCKET_IP_FREEBIND, 1}};
    if (ENVOY_SOCKET_SO_NOSIGPIPE.hasValue()) {
      names_vals.emplace_back(std::make_pair(ENVOY_SOCKET_SO_NOSIGPIPE, 1));
    }
    expectSetsockopts(names_vals);
  }

  void expectOnlyNoSigpipeOptions() {
    NameVals names_vals{{std::make_pair(ENVOY_SOCKET_SO_NOSIGPIPE, 1)}};
    expectSetsockopts(names_vals);
  }

  void expectNoSocketOptions() {
    EXPECT_CALL(factory_.tls_.dispatcher_, createClientConnection_(_, _, _, _))
        .WillOnce(
            Invoke([this](Network::Address::InstanceConstSharedPtr,
                          Network::Address::InstanceConstSharedPtr, Network::TransportSocketPtr&,
                          const Network::ConnectionSocket::OptionsSharedPtr& options)
                       -> Network::ClientConnection* {
              EXPECT_EQ(nullptr, options.get());
              return connection_;
            }));
    auto conn_data = cluster_manager_->getThreadLocalCluster("SockoptsCluster")->tcpConn(nullptr);
    EXPECT_EQ(connection_, conn_data.connection_.get());
  }

  Network::MockClientConnection* connection_ = new NiceMock<Network::MockClientConnection>();
};

TEST_F(SockoptsTest, SockoptsUnset) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: SockoptsCluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: SockoptsCluster
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
  )EOF";
  initialize(yaml);
  if (ENVOY_SOCKET_SO_NOSIGPIPE.hasValue()) {
    expectOnlyNoSigpipeOptions();
  } else {
    expectNoSocketOptions();
  }
}

TEST_F(SockoptsTest, FreebindClusterOnly) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: SockoptsCluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: SockoptsCluster
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
      upstream_bind_config:
        freebind: true
  )EOF";
  initialize(yaml);
  expectSetsockoptFreebind();
}

TEST_F(SockoptsTest, FreebindClusterManagerOnly) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: SockoptsCluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: SockoptsCluster
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
  cluster_manager:
    upstream_bind_config:
      freebind: true
  )EOF";
  initialize(yaml);
  expectSetsockoptFreebind();
}

TEST_F(SockoptsTest, FreebindClusterOverride) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: SockoptsCluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: SockoptsCluster
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
      upstream_bind_config:
        freebind: true
  cluster_manager:
    upstream_bind_config:
      freebind: false
  )EOF";
  initialize(yaml);
  expectSetsockoptFreebind();
}

TEST_F(SockoptsTest, SockoptsClusterOnly) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: SockoptsCluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: SockoptsCluster
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
      upstream_bind_config:
        socket_options: [
          { level: 1, name: 2, int_value: 3, state: STATE_PREBIND },
          { level: 4, name: 5, int_value: 6, state: STATE_PREBIND }]

  )EOF";
  initialize(yaml);
  NameVals names_vals{{ENVOY_MAKE_SOCKET_OPTION_NAME(1, 2), 3},
                      {ENVOY_MAKE_SOCKET_OPTION_NAME(4, 5), 6}};
  if (ENVOY_SOCKET_SO_NOSIGPIPE.hasValue()) {
    names_vals.emplace_back(std::make_pair(ENVOY_SOCKET_SO_NOSIGPIPE, 1));
  }
  expectSetsockopts(names_vals);
}

TEST_F(SockoptsTest, SockoptsClusterManagerOnly) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: SockoptsCluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: SockoptsCluster
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
  cluster_manager:
    upstream_bind_config:
      socket_options: [
        { level: 1, name: 2, int_value: 3, state: STATE_PREBIND },
        { level: 4, name: 5, int_value: 6, state: STATE_PREBIND }]
  )EOF";
  initialize(yaml);
  NameVals names_vals{{ENVOY_MAKE_SOCKET_OPTION_NAME(1, 2), 3},
                      {ENVOY_MAKE_SOCKET_OPTION_NAME(4, 5), 6}};
  if (ENVOY_SOCKET_SO_NOSIGPIPE.hasValue()) {
    names_vals.emplace_back(std::make_pair(ENVOY_SOCKET_SO_NOSIGPIPE, 1));
  }
  expectSetsockopts(names_vals);
}

TEST_F(SockoptsTest, SockoptsClusterOverride) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: SockoptsCluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: SockoptsCluster
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
      upstream_bind_config:
        socket_options: [
          { level: 1, name: 2, int_value: 3, state: STATE_PREBIND },
          { level: 4, name: 5, int_value: 6, state: STATE_PREBIND }]
  cluster_manager:
    upstream_bind_config:
      socket_options: [{ level: 7, name: 8, int_value: 9, state: STATE_PREBIND }]
  )EOF";
  initialize(yaml);
  NameVals names_vals{{ENVOY_MAKE_SOCKET_OPTION_NAME(1, 2), 3},
                      {ENVOY_MAKE_SOCKET_OPTION_NAME(4, 5), 6}};
  if (ENVOY_SOCKET_SO_NOSIGPIPE.hasValue()) {
    names_vals.emplace_back(std::make_pair(ENVOY_SOCKET_SO_NOSIGPIPE, 1));
  }
  expectSetsockopts(names_vals);
}

// Validate that when tcp keepalives are set in the Cluster, we see the socket
// option propagated to setsockopt(). This is as close to an end-to-end test as we have for this
// feature, due to the complexity of creating an integration test involving the network stack. We
// only test the IPv4 case here, as the logic around IPv4/IPv6 handling is tested generically in
// tcp_keepalive_option_impl_test.cc.
class TcpKeepaliveTest : public ClusterManagerImplTest {
public:
  void initialize(const std::string& yaml) { create(parseBootstrapFromV3Yaml(yaml)); }

  void TearDown() override { factory_.tls_.shutdownThread(); }

  void expectSetsockoptSoKeepalive(absl::optional<int> keepalive_probes,
                                   absl::optional<int> keepalive_time,
                                   absl::optional<int> keepalive_interval) {
    if (!ENVOY_SOCKET_SO_KEEPALIVE.hasValue()) {
      EXPECT_CALL(factory_.tls_.dispatcher_, createClientConnection_(_, _, _, _))
          .WillOnce(
              Invoke([this](Network::Address::InstanceConstSharedPtr,
                            Network::Address::InstanceConstSharedPtr, Network::TransportSocketPtr&,
                            const Network::ConnectionSocket::OptionsSharedPtr& options)
                         -> Network::ClientConnection* {
                EXPECT_NE(nullptr, options.get());
                EXPECT_EQ(1, options->size());
                NiceMock<Network::MockConnectionSocket> socket;
                EXPECT_FALSE((Network::Socket::applyOptions(
                    options, socket, envoy::config::core::v3::SocketOption::STATE_PREBIND)));
                return connection_;
              }));
      cluster_manager_->getThreadLocalCluster("TcpKeepaliveCluster")->tcpConn(nullptr);
      return;
    }
    NiceMock<Api::MockOsSysCalls> os_sys_calls;
    TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
    NiceMock<Network::MockConnectionSocket> socket;
    EXPECT_CALL(factory_.tls_.dispatcher_, createClientConnection_(_, _, _, _))
        .WillOnce(Invoke([this, &socket](Network::Address::InstanceConstSharedPtr,
                                         Network::Address::InstanceConstSharedPtr,
                                         Network::TransportSocketPtr&,
                                         const Network::ConnectionSocket::OptionsSharedPtr& options)
                             -> Network::ClientConnection* {
          EXPECT_NE(nullptr, options.get());
          EXPECT_TRUE((Network::Socket::applyOptions(
              options, socket, envoy::config::core::v3::SocketOption::STATE_PREBIND)));
          return connection_;
        }));
    if (ENVOY_SOCKET_SO_NOSIGPIPE.hasValue()) {
      EXPECT_CALL(socket, setSocketOption(ENVOY_SOCKET_SO_NOSIGPIPE.level(),
                                          ENVOY_SOCKET_SO_NOSIGPIPE.option(), _, sizeof(int)))
          .WillOnce(Invoke([](int, int, const void* optval, socklen_t) -> Api::SysCallIntResult {
            EXPECT_EQ(1, *static_cast<const int*>(optval));
            return {0, 0};
          }));
    }
    EXPECT_CALL(socket, setSocketOption(ENVOY_SOCKET_SO_KEEPALIVE.level(),
                                        ENVOY_SOCKET_SO_KEEPALIVE.option(), _, sizeof(int)))
        .WillOnce(Invoke([](int, int, const void* optval, socklen_t) -> Api::SysCallIntResult {
          EXPECT_EQ(1, *static_cast<const int*>(optval));
          return {0, 0};
        }));
    if (keepalive_probes.has_value()) {
      EXPECT_CALL(socket, setSocketOption(ENVOY_SOCKET_TCP_KEEPCNT.level(),
                                          ENVOY_SOCKET_TCP_KEEPCNT.option(), _, sizeof(int)))
          .WillOnce(Invoke([&keepalive_probes](int, int, const void* optval,
                                               socklen_t) -> Api::SysCallIntResult {
            EXPECT_EQ(keepalive_probes.value(), *static_cast<const int*>(optval));
            return {0, 0};
          }));
    }
    if (keepalive_time.has_value()) {
      EXPECT_CALL(socket, setSocketOption(ENVOY_SOCKET_TCP_KEEPIDLE.level(),
                                          ENVOY_SOCKET_TCP_KEEPIDLE.option(), _, sizeof(int)))
          .WillOnce(Invoke(
              [&keepalive_time](int, int, const void* optval, socklen_t) -> Api::SysCallIntResult {
                EXPECT_EQ(keepalive_time.value(), *static_cast<const int*>(optval));
                return {0, 0};
              }));
    }
    if (keepalive_interval.has_value()) {
      EXPECT_CALL(socket, setSocketOption(ENVOY_SOCKET_TCP_KEEPINTVL.level(),
                                          ENVOY_SOCKET_TCP_KEEPINTVL.option(), _, sizeof(int)))
          .WillOnce(Invoke([&keepalive_interval](int, int, const void* optval,
                                                 socklen_t) -> Api::SysCallIntResult {
            EXPECT_EQ(keepalive_interval.value(), *static_cast<const int*>(optval));
            return {0, 0};
          }));
    }
    auto conn_data =
        cluster_manager_->getThreadLocalCluster("TcpKeepaliveCluster")->tcpConn(nullptr);
    EXPECT_EQ(connection_, conn_data.connection_.get());
  }

  void expectOnlyNoSigpipeOptions() {
    NiceMock<Network::MockConnectionSocket> socket;
    EXPECT_CALL(factory_.tls_.dispatcher_, createClientConnection_(_, _, _, _))
        .WillOnce(Invoke([this, &socket](Network::Address::InstanceConstSharedPtr,
                                         Network::Address::InstanceConstSharedPtr,
                                         Network::TransportSocketPtr&,
                                         const Network::ConnectionSocket::OptionsSharedPtr& options)
                             -> Network::ClientConnection* {
          EXPECT_NE(nullptr, options.get());
          EXPECT_TRUE((Network::Socket::applyOptions(
              options, socket, envoy::config::core::v3::SocketOption::STATE_PREBIND)));
          return connection_;
        }));
    EXPECT_CALL(socket, setSocketOption(ENVOY_SOCKET_SO_NOSIGPIPE.level(),
                                        ENVOY_SOCKET_SO_NOSIGPIPE.option(), _, sizeof(int)))
        .WillOnce(Invoke([](int, int, const void* optval, socklen_t) -> Api::SysCallIntResult {
          EXPECT_EQ(1, *static_cast<const int*>(optval));
          return {0, 0};
        }));
    auto conn_data =
        cluster_manager_->getThreadLocalCluster("TcpKeepaliveCluster")->tcpConn(nullptr);
    EXPECT_EQ(connection_, conn_data.connection_.get());
  }

  void expectNoSocketOptions() {
    EXPECT_CALL(factory_.tls_.dispatcher_, createClientConnection_(_, _, _, _))
        .WillOnce(
            Invoke([this](Network::Address::InstanceConstSharedPtr,
                          Network::Address::InstanceConstSharedPtr, Network::TransportSocketPtr&,
                          const Network::ConnectionSocket::OptionsSharedPtr& options)
                       -> Network::ClientConnection* {
              EXPECT_EQ(nullptr, options.get());
              return connection_;
            }));
    auto conn_data =
        cluster_manager_->getThreadLocalCluster("TcpKeepaliveCluster")->tcpConn(nullptr);
    EXPECT_EQ(connection_, conn_data.connection_.get());
  }

  Network::MockClientConnection* connection_ = new NiceMock<Network::MockClientConnection>();
};

TEST_F(TcpKeepaliveTest, TcpKeepaliveUnset) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: TcpKeepaliveCluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: TcpKeepaliveCluster
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
  )EOF";
  initialize(yaml);
  if (ENVOY_SOCKET_SO_NOSIGPIPE.hasValue()) {
    expectOnlyNoSigpipeOptions();
  } else {
    expectNoSocketOptions();
  }
}

TEST_F(TcpKeepaliveTest, TcpKeepaliveCluster) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: TcpKeepaliveCluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: TcpKeepaliveCluster
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
      upstream_connection_options:
        tcp_keepalive: {}
  )EOF";
  initialize(yaml);
  expectSetsockoptSoKeepalive({}, {}, {});
}

TEST_F(TcpKeepaliveTest, TcpKeepaliveClusterProbes) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: TcpKeepaliveCluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: TcpKeepaliveCluster
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
      upstream_connection_options:
        tcp_keepalive:
          keepalive_probes: 7
  )EOF";
  initialize(yaml);
  expectSetsockoptSoKeepalive(7, {}, {});
}

TEST_F(TcpKeepaliveTest, TcpKeepaliveWithAllOptions) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: TcpKeepaliveCluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: TcpKeepaliveCluster
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
      upstream_connection_options:
        tcp_keepalive:
          keepalive_probes: 7
          keepalive_time: 4
          keepalive_interval: 1
  )EOF";
  initialize(yaml);
  expectSetsockoptSoKeepalive(7, 4, 1);
}

TEST_F(ClusterManagerImplTest, ConnPoolsDrainedOnHostSetChange) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      common_lb_config:
        close_connections_on_host_set_change: true
  )EOF";

  ReadyWatcher initialized;
  EXPECT_CALL(initialized, ready());

  create(parseBootstrapFromV3Yaml(yaml));

  // Set up for an initialize callback.
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

  std::unique_ptr<MockClusterUpdateCallbacks> callbacks(new NiceMock<MockClusterUpdateCallbacks>());
  ClusterUpdateCallbacksHandlePtr cb =
      cluster_manager_->addThreadLocalClusterUpdateCallbacks(*callbacks);

  const auto all_clusters = cluster_manager_->clusters();
  EXPECT_TRUE(all_clusters.warming_clusters_.empty());
  EXPECT_EQ(all_clusters.active_clusters_.size(), 1);
  EXPECT_FALSE(all_clusters.active_clusters_.at("cluster_1").get().info()->addedViaApi());

  // Verify that we get no hosts when the HostSet is empty.
  EXPECT_EQ(absl::nullopt,
            cluster_manager_->getThreadLocalCluster("cluster_1")
                ->httpConnPool(ResourcePriority::Default, Http::Protocol::Http11, nullptr));
  EXPECT_EQ(absl::nullopt, cluster_manager_->getThreadLocalCluster("cluster_1")
                               ->tcpConnPool(ResourcePriority::Default, nullptr));
  EXPECT_EQ(nullptr,
            cluster_manager_->getThreadLocalCluster("cluster_1")->tcpConn(nullptr).connection_);

  Cluster& cluster = cluster_manager_->activeClusters().begin()->second;

  // Set up the HostSet.
  HostSharedPtr host1 = makeTestHost(cluster.info(), "tcp://127.0.0.1:80", time_system_);
  HostSharedPtr host2 = makeTestHost(cluster.info(), "tcp://127.0.0.1:81", time_system_);

  HostVector hosts{host1, host2};
  auto hosts_ptr = std::make_shared<HostVector>(hosts);

  // Sending non-mergeable updates.
  cluster.prioritySet().updateHosts(
      0, HostSetImpl::partitionHosts(hosts_ptr, HostsPerLocalityImpl::empty()), nullptr, hosts, {},
      100);

  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());

  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _))
      .Times(3)
      .WillRepeatedly(ReturnNew<NiceMock<Http::ConnectionPool::MockInstance>>());

  EXPECT_CALL(factory_, allocateTcpConnPool_)
      .Times(3)
      .WillRepeatedly(ReturnNew<NiceMock<Tcp::ConnectionPool::MockInstance>>());

  // This should provide us a CP for each of the above hosts.
  Http::ConnectionPool::MockInstance* cp1 = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(ResourcePriority::Default, Http::Protocol::Http11, nullptr));
  // Create persistent connection for host2.
  Http::ConnectionPool::MockInstance* cp2 = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(ResourcePriority::Default, Http::Protocol::Http2, nullptr));

  Tcp::ConnectionPool::MockInstance* tcp1 =
      TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                   ->tcpConnPool(ResourcePriority::Default, nullptr));

  Tcp::ConnectionPool::MockInstance* tcp2 =
      TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                   ->tcpConnPool(ResourcePriority::Default, nullptr));

  EXPECT_NE(cp1, cp2);
  EXPECT_NE(tcp1, tcp2);

  EXPECT_CALL(*cp2, startDrain()).WillOnce(Invoke([&]() {
    cp2->idle_cb_();
    cp2->idle_cb_ = nullptr;
  }));
  EXPECT_CALL(*cp1, startDrain()).WillOnce(Invoke([&]() {
    cp1->idle_cb_();
    cp1->idle_cb_ = nullptr;
  }));
  EXPECT_CALL(*tcp1, startDrain()).WillOnce(Invoke([&]() {
    tcp1->idle_cb_();
    tcp1->idle_cb_ = nullptr;
  }));
  EXPECT_CALL(*tcp2, startDrain()).WillOnce(Invoke([&]() {
    tcp2->idle_cb_();
    tcp2->idle_cb_ = nullptr;
  }));

  HostVector hosts_removed;
  hosts_removed.push_back(host2);

  // This update should drain all connection pools (host1, host2).
  cluster.prioritySet().updateHosts(
      0, HostSetImpl::partitionHosts(hosts_ptr, HostsPerLocalityImpl::empty()), nullptr, {},
      hosts_removed, 100);

  // Recreate connection pool for host1.
  cp1 = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(ResourcePriority::Default, Http::Protocol::Http11, nullptr));

  tcp1 = TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                      ->tcpConnPool(ResourcePriority::Default, nullptr));

  HostSharedPtr host3 = makeTestHost(cluster.info(), "tcp://127.0.0.1:82", time_system_);

  HostVector hosts_added;
  hosts_added.push_back(host3);

  EXPECT_CALL(*cp1, startDrain()).WillOnce(Invoke([&]() {
    cp1->idle_cb_();
    cp1->idle_cb_ = nullptr;
  }));
  EXPECT_CALL(*tcp1, startDrain()).WillOnce(Invoke([&]() {
    tcp1->idle_cb_();
    tcp1->idle_cb_ = nullptr;
  }));

  // Adding host3 should drain connection pool for host1.
  cluster.prioritySet().updateHosts(
      0, HostSetImpl::partitionHosts(hosts_ptr, HostsPerLocalityImpl::empty()), nullptr,
      hosts_added, {}, 100);
}

TEST_F(ClusterManagerImplTest, ConnPoolsNotDrainedOnHostSetChange) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
  )EOF";

  ReadyWatcher initialized;
  EXPECT_CALL(initialized, ready());
  create(parseBootstrapFromV3Yaml(yaml));

  // Set up for an initialize callback.
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

  std::unique_ptr<MockClusterUpdateCallbacks> callbacks(new NiceMock<MockClusterUpdateCallbacks>());
  ClusterUpdateCallbacksHandlePtr cb =
      cluster_manager_->addThreadLocalClusterUpdateCallbacks(*callbacks);

  Cluster& cluster = cluster_manager_->activeClusters().begin()->second;

  // Set up the HostSet.
  HostSharedPtr host1 = makeTestHost(cluster.info(), "tcp://127.0.0.1:80", time_system_);

  HostVector hosts{host1};
  auto hosts_ptr = std::make_shared<HostVector>(hosts);

  // Sending non-mergeable updates.
  cluster.prioritySet().updateHosts(
      0, HostSetImpl::partitionHosts(hosts_ptr, HostsPerLocalityImpl::empty()), nullptr, hosts, {},
      100);

  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _))
      .Times(1)
      .WillRepeatedly(ReturnNew<NiceMock<Http::ConnectionPool::MockInstance>>());

  EXPECT_CALL(factory_, allocateTcpConnPool_)
      .Times(1)
      .WillRepeatedly(ReturnNew<NiceMock<Tcp::ConnectionPool::MockInstance>>());

  // This should provide us a CP for each of the above hosts.
  Http::ConnectionPool::MockInstance* cp1 = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(ResourcePriority::Default, Http::Protocol::Http11, nullptr));

  Tcp::ConnectionPool::MockInstance* tcp1 =
      TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                   ->tcpConnPool(ResourcePriority::Default, nullptr));

  HostSharedPtr host2 = makeTestHost(cluster.info(), "tcp://127.0.0.1:82", time_system_);
  HostVector hosts_added;
  hosts_added.push_back(host2);

  // No connection pools should be drained.
  EXPECT_CALL(*cp1, drainConnections()).Times(0);
  EXPECT_CALL(*tcp1, drainConnections()).Times(0);

  // No connection pools should be drained.
  cluster.prioritySet().updateHosts(
      0, HostSetImpl::partitionHosts(hosts_ptr, HostsPerLocalityImpl::empty()), nullptr,
      hosts_added, {}, 100);
}

TEST_F(ClusterManagerImplTest, ConnPoolsIdleDeleted) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.conn_pool_delete_when_idle", "true"}});

  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.25s
      lb_policy: ROUND_ROBIN
      type: STATIC
  )EOF";

  ReadyWatcher initialized;
  EXPECT_CALL(initialized, ready());
  create(parseBootstrapFromV3Yaml(yaml));

  // Set up for an initialize callback.
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

  std::unique_ptr<MockClusterUpdateCallbacks> callbacks(new NiceMock<MockClusterUpdateCallbacks>());
  ClusterUpdateCallbacksHandlePtr cb =
      cluster_manager_->addThreadLocalClusterUpdateCallbacks(*callbacks);

  Cluster& cluster = cluster_manager_->activeClusters().begin()->second;

  // Set up the HostSet.
  HostSharedPtr host1 = makeTestHost(cluster.info(), "tcp://127.0.0.1:80", time_system_);

  HostVector hosts{host1};
  auto hosts_ptr = std::make_shared<HostVector>(hosts);

  // Sending non-mergeable updates.
  cluster.prioritySet().updateHosts(
      0, HostSetImpl::partitionHosts(hosts_ptr, HostsPerLocalityImpl::empty()), nullptr, hosts, {},
      100);

  {
    auto* cp1 = new NiceMock<Http::ConnectionPool::MockInstance>();
    EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _)).WillOnce(Return(cp1));
    std::function<void()> idle_callback;
    EXPECT_CALL(*cp1, addIdleCallback(_)).WillOnce(SaveArg<0>(&idle_callback));

    EXPECT_EQ(cp1, HttpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                                 ->httpConnPool(ResourcePriority::Default,
                                                                Http::Protocol::Http11, nullptr)));
    // Request the same pool again and verify that it produces the same output
    EXPECT_EQ(cp1, HttpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                                 ->httpConnPool(ResourcePriority::Default,
                                                                Http::Protocol::Http11, nullptr)));

    // Trigger the idle callback so we remove the connection pool
    idle_callback();

    auto* cp2 = new NiceMock<Http::ConnectionPool::MockInstance>();
    EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _)).WillOnce(Return(cp2));
    EXPECT_CALL(*cp2, addIdleCallback(_));

    // This time we expect cp2 since cp1 will have been destroyed
    EXPECT_EQ(cp2, HttpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                                 ->httpConnPool(ResourcePriority::Default,
                                                                Http::Protocol::Http11, nullptr)));
  }

  {
    auto* tcp1 = new NiceMock<Tcp::ConnectionPool::MockInstance>();
    EXPECT_CALL(factory_, allocateTcpConnPool_).WillOnce(Return(tcp1));
    std::function<void()> idle_callback;
    EXPECT_CALL(*tcp1, addIdleCallback(_)).WillOnce(SaveArg<0>(&idle_callback));
    EXPECT_EQ(tcp1,
              TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                           ->tcpConnPool(ResourcePriority::Default, nullptr)));
    // Request the same pool again and verify that it produces the same output
    EXPECT_EQ(tcp1,
              TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                           ->tcpConnPool(ResourcePriority::Default, nullptr)));

    // Trigger the idle callback so we remove the connection pool
    idle_callback();

    auto* tcp2 = new NiceMock<Tcp::ConnectionPool::MockInstance>();
    EXPECT_CALL(factory_, allocateTcpConnPool_).WillOnce(Return(tcp2));

    // This time we expect tcp2 since tcp1 will have been destroyed
    EXPECT_EQ(tcp2,
              TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                           ->tcpConnPool(ResourcePriority::Default, nullptr)));
  }
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
    EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _))
        .WillOnce(Return(conn_pool_vector.back()));
    EXPECT_CALL(downstream_connection, hashKey)
        .WillOnce(Invoke([i](std::vector<uint8_t>& hash_key) { hash_key.push_back(i); }));
    EXPECT_EQ(conn_pool_vector.back(),
              HttpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                            ->httpConnPool(ResourcePriority::Default,
                                                           Http::Protocol::Http11, &lb_context)));
  }

  // Check that the first entry is still in the pool map
  EXPECT_CALL(downstream_connection, hashKey).WillOnce(Invoke([](std::vector<uint8_t>& hash_key) {
    hash_key.push_back(0);
  }));
  EXPECT_EQ(conn_pool_vector.front(),
            HttpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                          ->httpConnPool(ResourcePriority::Default,
                                                         Http::Protocol::Http11, &lb_context)));
}

class PreconnectTest : public ClusterManagerImplTest {
public:
  void initialize(float ratio) {
    const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
  )EOF";

    ReadyWatcher initialized;
    EXPECT_CALL(initialized, ready());
    envoy::config::bootstrap::v3::Bootstrap config = parseBootstrapFromV3Yaml(yaml);
    if (ratio != 0) {
      config.mutable_static_resources()
          ->mutable_clusters(0)
          ->mutable_preconnect_policy()
          ->mutable_predictive_preconnect_ratio()
          ->set_value(ratio);
    }
    create(config);

    // Set up for an initialize callback.
    cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

    std::unique_ptr<MockClusterUpdateCallbacks> callbacks(
        new NiceMock<MockClusterUpdateCallbacks>());
    ClusterUpdateCallbacksHandlePtr cb =
        cluster_manager_->addThreadLocalClusterUpdateCallbacks(*callbacks);

    cluster_ = &cluster_manager_->activeClusters().begin()->second.get();

    // Set up the HostSet.
    host1_ = makeTestHost(cluster_->info(), "tcp://127.0.0.1:80", time_system_);
    host2_ = makeTestHost(cluster_->info(), "tcp://127.0.0.1:80", time_system_);
    host3_ = makeTestHost(cluster_->info(), "tcp://127.0.0.1:80", time_system_);
    host4_ = makeTestHost(cluster_->info(), "tcp://127.0.0.1:80", time_system_);

    HostVector hosts{host1_, host2_, host3_, host4_};
    auto hosts_ptr = std::make_shared<HostVector>(hosts);

    // Sending non-mergeable updates.
    cluster_->prioritySet().updateHosts(
        0, HostSetImpl::partitionHosts(hosts_ptr, HostsPerLocalityImpl::empty()), nullptr, hosts,
        {}, 100);
  }

  Cluster* cluster_{};
  HostSharedPtr host1_;
  HostSharedPtr host2_;
  HostSharedPtr host3_;
  HostSharedPtr host4_;
  Http::MockResponseDecoder decoder_;
  Http::ConnectionPool::MockCallbacks http_callbacks_;
  Tcp::ConnectionPool::MockCallbacks tcp_callbacks_;
};

TEST_F(PreconnectTest, PreconnectOff) {
  // With preconnect set to 0, each request for a connection pool will only
  // allocate that conn pool.
  initialize(0);
  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _))
      .Times(1)
      .WillRepeatedly(ReturnNew<NiceMock<Http::ConnectionPool::MockInstance>>());
  auto http_handle = cluster_manager_->getThreadLocalCluster("cluster_1")
                         ->httpConnPool(ResourcePriority::Default, Http::Protocol::Http11, nullptr);
  http_handle.value().newStream(decoder_, http_callbacks_);

  EXPECT_CALL(factory_, allocateTcpConnPool_(_))
      .Times(1)
      .WillRepeatedly(ReturnNew<NiceMock<Tcp::ConnectionPool::MockInstance>>());
  auto tcp_handle = cluster_manager_->getThreadLocalCluster("cluster_1")
                        ->tcpConnPool(ResourcePriority::Default, nullptr);
  ASSERT_TRUE(tcp_handle.has_value());
  tcp_handle.value().newConnection(tcp_callbacks_);
}

TEST_F(PreconnectTest, PreconnectOn) {
  // With preconnect set to 1.1, maybePreconnect will kick off
  // preconnecting, so create the pool for both the current connection and the
  // anticipated one.
  initialize(1.1);
  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _))
      .Times(2)
      .WillRepeatedly(ReturnNew<NiceMock<Http::ConnectionPool::MockInstance>>());
  auto http_handle = cluster_manager_->getThreadLocalCluster("cluster_1")
                         ->httpConnPool(ResourcePriority::Default, Http::Protocol::Http11, nullptr);
  http_handle.value().newStream(decoder_, http_callbacks_);

  EXPECT_CALL(factory_, allocateTcpConnPool_)
      .Times(2)
      .WillRepeatedly(ReturnNew<NiceMock<Tcp::ConnectionPool::MockInstance>>());
  auto tcp_handle = cluster_manager_->getThreadLocalCluster("cluster_1")
                        ->tcpConnPool(ResourcePriority::Default, nullptr);
  ASSERT_TRUE(tcp_handle.has_value());
  tcp_handle.value().newConnection(tcp_callbacks_);
}

TEST_F(PreconnectTest, PreconnectHighHttp) {
  // With preconnect set to 3, the first request will kick off 3 preconnect attempts.
  initialize(3);
  int http_preconnect = 0;
  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _))
      .Times(4)
      .WillRepeatedly(InvokeWithoutArgs([&]() -> Http::ConnectionPool::Instance* {
        auto* ret = new NiceMock<Http::ConnectionPool::MockInstance>();
        ON_CALL(*ret, maybePreconnect(_)).WillByDefault(InvokeWithoutArgs([&]() -> bool {
          ++http_preconnect;
          return true;
        }));
        return ret;
      }));
  auto http_handle = cluster_manager_->getThreadLocalCluster("cluster_1")
                         ->httpConnPool(ResourcePriority::Default, Http::Protocol::Http11, nullptr);
  http_handle.value().newStream(decoder_, http_callbacks_);
  // Expect preconnect to be called 3 times across the four hosts.
  EXPECT_EQ(3, http_preconnect);
}

TEST_F(PreconnectTest, PreconnectHighTcp) {
  // With preconnect set to 3, the first request will kick off 3 preconnect attempts.
  initialize(3);
  int tcp_preconnect = 0;
  EXPECT_CALL(factory_, allocateTcpConnPool_(_))
      .Times(4)
      .WillRepeatedly(InvokeWithoutArgs([&]() -> Tcp::ConnectionPool::Instance* {
        auto* ret = new NiceMock<Tcp::ConnectionPool::MockInstance>();
        ON_CALL(*ret, maybePreconnect(_)).WillByDefault(InvokeWithoutArgs([&]() -> bool {
          ++tcp_preconnect;
          return true;
        }));
        return ret;
      }));
  auto tcp_handle = cluster_manager_->getThreadLocalCluster("cluster_1")
                        ->tcpConnPool(ResourcePriority::Default, nullptr);
  tcp_handle.value().newConnection(tcp_callbacks_);
  // Expect preconnect to be called 3 times across the four hosts.
  EXPECT_EQ(3, tcp_preconnect);
}

TEST_F(PreconnectTest, PreconnectCappedAt3) {
  // With preconnect set to 20, no more than 3 connections will be preconnected.
  initialize(20);
  int http_preconnect = 0;
  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _))
      .Times(4)
      .WillRepeatedly(InvokeWithoutArgs([&]() -> Http::ConnectionPool::Instance* {
        auto* ret = new NiceMock<Http::ConnectionPool::MockInstance>();
        ON_CALL(*ret, maybePreconnect(_)).WillByDefault(InvokeWithoutArgs([&]() -> bool {
          ++http_preconnect;
          return true;
        }));
        return ret;
      }));
  auto http_handle = cluster_manager_->getThreadLocalCluster("cluster_1")
                         ->httpConnPool(ResourcePriority::Default, Http::Protocol::Http11, nullptr);
  http_handle.value().newStream(decoder_, http_callbacks_);
  // Expect preconnect to be called 3 times across the four hosts.
  EXPECT_EQ(3, http_preconnect);

  // A subsequent call to get a connection will consume one of the preconnected
  // connections, leaving two in queue, and kick off 2 more. This time we won't
  // do the full 3 as the number of outstanding preconnects is limited by the
  // number of healthy hosts.
  http_preconnect = 0;
  http_handle = cluster_manager_->getThreadLocalCluster("cluster_1")
                    ->httpConnPool(ResourcePriority::Default, Http::Protocol::Http11, nullptr);
  http_handle.value().newStream(decoder_, http_callbacks_);
  EXPECT_EQ(2, http_preconnect);
}

TEST_F(PreconnectTest, PreconnectCappedByMaybePreconnect) {
  // Set preconnect high, and verify preconnecting stops when maybePreconnect returns false.
  initialize(20);
  int http_preconnect_calls = 0;
  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _))
      .Times(2)
      .WillRepeatedly(InvokeWithoutArgs([&]() -> Http::ConnectionPool::Instance* {
        auto* ret = new NiceMock<Http::ConnectionPool::MockInstance>();
        ON_CALL(*ret, maybePreconnect(_)).WillByDefault(InvokeWithoutArgs([&]() -> bool {
          ++http_preconnect_calls;
          // Force maybe preconnect to fail.
          return false;
        }));
        return ret;
      }));
  auto http_handle = cluster_manager_->getThreadLocalCluster("cluster_1")
                         ->httpConnPool(ResourcePriority::Default, Http::Protocol::Http11, nullptr);
  http_handle.value().newStream(decoder_, http_callbacks_);
  // Expect preconnect to be called once and then preconnecting is stopped.
  EXPECT_EQ(1, http_preconnect_calls);
}

} // namespace
} // namespace Upstream
} // namespace Envoy
