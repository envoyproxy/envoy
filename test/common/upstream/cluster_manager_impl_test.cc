#include <memory>
#include <string>

#include "envoy/admin/v2alpha/config_dump.pb.h"
#include "envoy/network/listen_socket.h"
#include "envoy/upstream/upstream.h"

#include "common/api/api_impl.h"
#include "common/config/bootstrap_json.h"
#include "common/config/utility.h"
#include "common/network/socket_option_impl.h"
#include "common/network/transport_socket_options_impl.h"
#include "common/network/utility.h"
#include "common/ssl/context_manager_impl.h"
#include "common/upstream/cluster_manager_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/access_log/mocks.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/secret/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/tcp/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::Mock;
using testing::NiceMock;
using testing::Pointee;
using testing::Return;
using testing::ReturnNew;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Upstream {
namespace {

// The tests in this file are split between testing with real clusters and some with mock clusters.
// By default we setup to call the real cluster creation function. Individual tests can override
// the expectations when needed.
class TestClusterManagerFactory : public ClusterManagerFactory {
public:
  TestClusterManagerFactory() {
    ON_CALL(*this, clusterFromProto_(_, _, _, _, _))
        .WillByDefault(Invoke([&](const envoy::api::v2::Cluster& cluster, ClusterManager& cm,
                                  Outlier::EventLoggerSharedPtr outlier_event_logger,
                                  AccessLog::AccessLogManager& log_manager,
                                  bool added_via_api) -> ClusterSharedPtr {
          return ClusterImplBase::create(
              cluster, cm, stats_, tls_, dns_resolver_, ssl_context_manager_, runtime_, random_,
              dispatcher_, log_manager, local_info_, outlier_event_logger, added_via_api);
        }));
  }

  Http::ConnectionPool::InstancePtr
  allocateConnPool(Event::Dispatcher&, HostConstSharedPtr host, ResourcePriority, Http::Protocol,
                   const Network::ConnectionSocket::OptionsSharedPtr&) override {
    return Http::ConnectionPool::InstancePtr{allocateConnPool_(host)};
  }

  Tcp::ConnectionPool::InstancePtr
  allocateTcpConnPool(Event::Dispatcher&, HostConstSharedPtr host, ResourcePriority,
                      const Network::ConnectionSocket::OptionsSharedPtr&,
                      Network::TransportSocketOptionsSharedPtr) override {
    return Tcp::ConnectionPool::InstancePtr{allocateTcpConnPool_(host)};
  }

  ClusterSharedPtr clusterFromProto(const envoy::api::v2::Cluster& cluster, ClusterManager& cm,
                                    Outlier::EventLoggerSharedPtr outlier_event_logger,
                                    AccessLog::AccessLogManager& log_manager,
                                    bool added_via_api) override {
    return clusterFromProto_(cluster, cm, outlier_event_logger, log_manager, added_via_api);
  }

  CdsApiPtr createCds(const envoy::api::v2::core::ConfigSource&,
                      const absl::optional<envoy::api::v2::core::ConfigSource>&,
                      ClusterManager&) override {
    return CdsApiPtr{createCds_()};
  }

  ClusterManagerPtr
  clusterManagerFromProto(const envoy::config::bootstrap::v2::Bootstrap& bootstrap,
                          Stats::Store& stats, ThreadLocal::Instance& tls, Runtime::Loader& runtime,
                          Runtime::RandomGenerator& random, const LocalInfo::LocalInfo& local_info,
                          AccessLog::AccessLogManager& log_manager, Server::Admin& admin) override {
    return ClusterManagerPtr{clusterManagerFromProto_(bootstrap, stats, tls, runtime, random,
                                                      local_info, log_manager, admin)};
  }

  Secret::SecretManager& secretManager() override { return secret_manager_; }

  MOCK_METHOD8(clusterManagerFromProto_,
               ClusterManager*(const envoy::config::bootstrap::v2::Bootstrap& bootstrap,
                               Stats::Store& stats, ThreadLocal::Instance& tls,
                               Runtime::Loader& runtime, Runtime::RandomGenerator& random,
                               const LocalInfo::LocalInfo& local_info,
                               AccessLog::AccessLogManager& log_manager, Server::Admin& admin));
  MOCK_METHOD1(allocateConnPool_, Http::ConnectionPool::Instance*(HostConstSharedPtr host));
  MOCK_METHOD1(allocateTcpConnPool_, Tcp::ConnectionPool::Instance*(HostConstSharedPtr host));
  MOCK_METHOD5(clusterFromProto_,
               ClusterSharedPtr(const envoy::api::v2::Cluster& cluster, ClusterManager& cm,
                                Outlier::EventLoggerSharedPtr outlier_event_logger,
                                AccessLog::AccessLogManager& log_manager, bool added_via_api));
  MOCK_METHOD0(createCds_, CdsApi*());

  Stats::IsolatedStoreImpl stats_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  std::shared_ptr<NiceMock<Network::MockDnsResolver>> dns_resolver_{
      new NiceMock<Network::MockDnsResolver>};
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Ssl::ContextManagerImpl ssl_context_manager_{dispatcher_.timeSystem()};
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Secret::MockSecretManager> secret_manager_;
};

// Helper to intercept calls to postThreadLocalClusterUpdate.
class MockLocalClusterUpdate {
public:
  MOCK_METHOD3(post, void(uint32_t priority, const HostVector& hosts_added,
                          const HostVector& hosts_removed));
};

// Override postThreadLocalClusterUpdate so we can test that merged updates calls
// it with the right values at the right times.
class TestClusterManagerImpl : public ClusterManagerImpl {
public:
  TestClusterManagerImpl(const envoy::config::bootstrap::v2::Bootstrap& bootstrap,
                         ClusterManagerFactory& factory, Stats::Store& stats,
                         ThreadLocal::Instance& tls, Runtime::Loader& runtime,
                         Runtime::RandomGenerator& random, const LocalInfo::LocalInfo& local_info,
                         AccessLog::AccessLogManager& log_manager,
                         Event::Dispatcher& main_thread_dispatcher, Server::Admin& admin,
                         Api::Api& api, MockLocalClusterUpdate& local_cluster_update)
      : ClusterManagerImpl(bootstrap, factory, stats, tls, runtime, random, local_info, log_manager,
                           main_thread_dispatcher, admin, api),
        local_cluster_update_(local_cluster_update) {}

protected:
  void postThreadLocalClusterUpdate(const Cluster&, uint32_t priority,
                                    const HostVector& hosts_added,
                                    const HostVector& hosts_removed) override {
    local_cluster_update_.post(priority, hosts_added, hosts_removed);
  }
  MockLocalClusterUpdate& local_cluster_update_;
};

envoy::config::bootstrap::v2::Bootstrap parseBootstrapFromV2Yaml(const std::string& yaml) {
  envoy::config::bootstrap::v2::Bootstrap bootstrap;
  MessageUtil::loadFromYaml(yaml, bootstrap);
  return bootstrap;
}

class ClusterManagerImplTest : public testing::Test {
public:
  ClusterManagerImplTest() : api_(Api::createApiForTest()) {
    factory_.dispatcher_.setTimeSystem(time_system_);
  }

  void create(const envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
    cluster_manager_ = std::make_unique<ClusterManagerImpl>(
        bootstrap, factory_, factory_.stats_, factory_.tls_, factory_.runtime_, factory_.random_,
        factory_.local_info_, log_manager_, factory_.dispatcher_, admin_, *api_);
  }

  void createWithLocalClusterUpdate(const bool enable_merge_window = true) {
    std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.250s
      type: STATIC
      lb_policy: ROUND_ROBIN
      hosts:
      - socket_address:
          address: "127.0.0.1"
          port_value: 11001
      - socket_address:
          address: "127.0.0.1"
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

    const auto& bootstrap = parseBootstrapFromV2Yaml(yaml);

    cluster_manager_ = std::make_unique<TestClusterManagerImpl>(
        bootstrap, factory_, factory_.stats_, factory_.tls_, factory_.runtime_, factory_.random_,
        factory_.local_info_, log_manager_, factory_.dispatcher_, admin_, *api_,
        local_cluster_update_);
  }

  void checkStats(uint64_t added, uint64_t modified, uint64_t removed, uint64_t active,
                  uint64_t warming) {
    EXPECT_EQ(added, factory_.stats_.counter("cluster_manager.cluster_added").value());
    EXPECT_EQ(modified, factory_.stats_.counter("cluster_manager.cluster_modified").value());
    EXPECT_EQ(removed, factory_.stats_.counter("cluster_manager.cluster_removed").value());
    EXPECT_EQ(active, factory_.stats_.gauge("cluster_manager.active_clusters").value());
    EXPECT_EQ(warming, factory_.stats_.gauge("cluster_manager.warming_clusters").value());
  }

  void checkConfigDump(const std::string& expected_dump_yaml) {
    auto message_ptr = admin_.config_tracker_.config_tracker_callbacks_["clusters"]();
    const auto& clusters_config_dump =
        dynamic_cast<const envoy::admin::v2alpha::ClustersConfigDump&>(*message_ptr);

    envoy::admin::v2alpha::ClustersConfigDump expected_clusters_config_dump;
    MessageUtil::loadFromYaml(expected_dump_yaml, expected_clusters_config_dump);
    EXPECT_EQ(expected_clusters_config_dump.DebugString(), clusters_config_dump.DebugString());
  }

  envoy::api::v2::core::Metadata buildMetadata(const std::string& version) const {
    envoy::api::v2::core::Metadata metadata;

    if (version != "") {
      Envoy::Config::Metadata::mutableMetadataValue(
          metadata, Config::MetadataFilters::get().ENVOY_LB, "version")
          .set_string_value(version);
    }

    return metadata;
  }

  Api::ApiPtr api_;
  NiceMock<TestClusterManagerFactory> factory_;
  std::unique_ptr<ClusterManagerImpl> cluster_manager_;
  AccessLog::MockAccessLogManager log_manager_;
  NiceMock<Server::MockAdmin> admin_;
  Event::SimulatedTimeSystem time_system_;
  MockLocalClusterUpdate local_cluster_update_;
};

envoy::config::bootstrap::v2::Bootstrap parseBootstrapFromJson(const std::string& json_string) {
  envoy::config::bootstrap::v2::Bootstrap bootstrap;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Stats::StatsOptionsImpl stats_options;
  Config::BootstrapJson::translateClusterManagerBootstrap(*json_object_ptr, bootstrap,
                                                          stats_options);
  return bootstrap;
}

TEST_F(ClusterManagerImplTest, MultipleProtocolClusterFail) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: http12_cluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      http2_protocol_options: {}
      http_protocol_options: {}
  )EOF";
  EXPECT_THROW_WITH_MESSAGE(
      create(parseBootstrapFromV2Yaml(yaml)), EnvoyException,
      "cluster: Both HTTP1 and HTTP2 options may only be configured with non-default "
      "'protocol_selection' values");
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

  EXPECT_THROW_WITH_MESSAGE(create(parseBootstrapFromV2Yaml(yaml)), EnvoyException,
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
  create(parseBootstrapFromV2Yaml(yaml));
  checkConfigDump(R"EOF(
static_clusters:
  - cluster:
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
}

TEST_F(ClusterManagerImplTest, OutlierEventLog) {
  const std::string json = R"EOF(
  {
    "outlier_detection": {
      "event_log_path": "foo"
    },
    "clusters": []
  }
  )EOF";

  EXPECT_CALL(log_manager_, createAccessLog("foo"));
  create(parseBootstrapFromJson(json));
}

TEST_F(ClusterManagerImplTest, NoSdsConfig) {
  const std::string json = fmt::sprintf("{%s}", clustersJson({defaultSdsClusterJson("cluster_1")}));
  EXPECT_THROW_WITH_MESSAGE(create(parseBootstrapFromJson(json)), EnvoyException,
                            "cannot create sds cluster with no sds config");
}

TEST_F(ClusterManagerImplTest, UnknownClusterType) {
  const std::string json = R"EOF(
  {
    "clusters": [
    {
      "name": "cluster_1",
      "connect_timeout_ms": 250,
      "type": "foo",
      "lb_type": "round_robin"
    }]
  }
  )EOF";

  EXPECT_THROW(create(parseBootstrapFromJson(json)), EnvoyException);
}

TEST_F(ClusterManagerImplTest, LocalClusterNotDefined) {
  const std::string json = fmt::sprintf(
      R"EOF(
  {
    "local_cluster_name": "new_cluster",
    %s
  }
  )EOF",
      clustersJson({defaultStaticClusterJson("cluster_1"), defaultStaticClusterJson("cluster_2")}));

  EXPECT_THROW(create(parseBootstrapFromJson(json)), EnvoyException);
}

TEST_F(ClusterManagerImplTest, BadClusterManagerConfig) {
  const std::string json = R"EOF(
  {
    "outlier_detection": {
      "event_log_path": "foo"
    },
    "clusters": [],
    "fake_property" : "fake_property"
  }
  )EOF";

  EXPECT_THROW(create(parseBootstrapFromJson(json)), Json::Exception);
}

TEST_F(ClusterManagerImplTest, LocalClusterDefined) {
  const std::string json = fmt::sprintf(
      R"EOF(
  {
    "local_cluster_name": "new_cluster",
    %s
  }
  )EOF",
      clustersJson({defaultStaticClusterJson("cluster_1"), defaultStaticClusterJson("cluster_2"),
                    defaultStaticClusterJson("new_cluster")}));

  create(parseBootstrapFromJson(json));
  checkStats(3 /*added*/, 0 /*modified*/, 0 /*removed*/, 3 /*active*/, 0 /*warming*/);

  factory_.tls_.shutdownThread();
}

TEST_F(ClusterManagerImplTest, DuplicateCluster) {
  const std::string json = fmt::sprintf(
      "{%s}",
      clustersJson({defaultStaticClusterJson("cluster_1"), defaultStaticClusterJson("cluster_1")}));
  EXPECT_THROW(create(parseBootstrapFromJson(json)), EnvoyException);
}

TEST_F(ClusterManagerImplTest, UnknownHcType) {
  const std::string json = R"EOF(
  {
    "clusters": [
    {
      "name": "cluster_1",
      "connect_timeout_ms": 250,
      "type": "static",
      "lb_type": "round_robin",
      "hosts": [{"url": "tcp://127.0.0.1:11001"}],
      "health_check": {
        "type": "foo"
      }
    }]
  }
  )EOF";

  EXPECT_THROW(create(parseBootstrapFromJson(json)), EnvoyException);
}

TEST_F(ClusterManagerImplTest, ValidClusterName) {
  const std::string json = R"EOF(
  {
    "clusters": [
    {
      "name": "cluster:name",
      "connect_timeout_ms": 250,
      "type": "static",
      "lb_type": "round_robin",
      "hosts": [{"url": "tcp://127.0.0.1:11001"}]
    }]
  }
  )EOF";

  create(parseBootstrapFromJson(json));
  cluster_manager_->clusters()
      .find("cluster:name")
      ->second.get()
      .info()
      ->statsScope()
      .counter("foo")
      .inc();
  EXPECT_EQ(1UL, factory_.stats_.counter("cluster.cluster_name.foo").value());
}

TEST_F(ClusterManagerImplTest, OriginalDstLbRestriction) {
  const std::string json = R"EOF(
  {
    "clusters": [
    {
      "name": "cluster_1",
      "connect_timeout_ms": 250,
      "type": "original_dst",
      "lb_type": "round_robin"
    }]
  }
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      create(parseBootstrapFromJson(json)), EnvoyException,
      "cluster: cluster type 'original_dst' may only be used with LB type 'original_dst_lb'");
}

TEST_F(ClusterManagerImplTest, OriginalDstLbRestriction2) {
  const std::string json = R"EOF(
  {
    "clusters": [
    {
      "name": "cluster_1",
      "connect_timeout_ms": 250,
      "type": "static",
      "lb_type": "original_dst_lb",
      "hosts": [{"url": "tcp://127.0.0.1:11001"}]
    }]
  }
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      create(parseBootstrapFromJson(json)), EnvoyException,
      "cluster: LB type 'original_dst_lb' may only be used with cluser type 'original_dst'");
}

TEST_F(ClusterManagerImplTest, SubsetLoadBalancerInitialization) {
  const std::string json = R"EOF(
  {
    "clusters": [
    {
      "name": "cluster_1",
      "connect_timeout_ms": 250,
      "type": "static",
      "lb_type": "round_robin",
      "hosts": [{"url": "tcp://127.0.0.1:8000"}, {"url": "tcp://127.0.0.1:8001"}]
    }]
  }
  )EOF";

  envoy::config::bootstrap::v2::Bootstrap bootstrap = parseBootstrapFromJson(json);
  envoy::api::v2::Cluster::LbSubsetConfig* subset_config =
      bootstrap.mutable_static_resources()->mutable_clusters(0)->mutable_lb_subset_config();
  subset_config->set_fallback_policy(envoy::api::v2::Cluster::LbSubsetConfig::ANY_ENDPOINT);
  subset_config->add_subset_selectors()->add_keys("x");

  create(bootstrap);
  checkStats(1 /*added*/, 0 /*modified*/, 0 /*removed*/, 1 /*active*/, 0 /*warming*/);

  factory_.tls_.shutdownThread();
}

TEST_F(ClusterManagerImplTest, SubsetLoadBalancerRestriction) {
  const std::string json = R"EOF(
  {
    "clusters": [
    {
      "name": "cluster_1",
      "connect_timeout_ms": 250,
      "type": "original_dst",
      "lb_type": "original_dst_lb"
    }]
  }
  )EOF";

  envoy::config::bootstrap::v2::Bootstrap bootstrap = parseBootstrapFromJson(json);
  envoy::api::v2::Cluster::LbSubsetConfig* subset_config =
      bootstrap.mutable_static_resources()->mutable_clusters(0)->mutable_lb_subset_config();
  subset_config->set_fallback_policy(envoy::api::v2::Cluster::LbSubsetConfig::ANY_ENDPOINT);
  subset_config->add_subset_selectors()->add_keys("x");

  EXPECT_THROW_WITH_MESSAGE(
      create(bootstrap), EnvoyException,
      "cluster: cluster type 'original_dst' may not be used with lb_subset_config");
}

TEST_F(ClusterManagerImplTest, RingHashLoadBalancerInitialization) {
  const std::string json = R"EOF(
  {
    "clusters": [{
      "name": "redis_cluster",
      "lb_type": "ring_hash",
      "ring_hash_lb_config": {
        "minimum_ring_size": 125,
        "use_std_hash": true
      },
      "connect_timeout_ms": 250,
      "type": "static",
      "hosts": [{"url": "tcp://127.0.0.1:8000"}, {"url": "tcp://127.0.0.1:8001"}]
    }]
  }
  )EOF";
  create(parseBootstrapFromJson(json));
}

TEST_F(ClusterManagerImplTest, RingHashLoadBalancerV2Initialization) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: redis_cluster
      connect_timeout: 0.250s
      lb_policy: RING_HASH
      hosts:
      - socket_address:
          address: 127.0.0.1
          port_value: 8000
      - socket_address:
          address: 127.0.0.1
          port_value: 8001
      dns_lookup_family: V4_ONLY
      ring_hash_lb_config:
        minimum_ring_size: 125
        deprecated_v1:
          use_std_hash: true
  )EOF";
  create(parseBootstrapFromV2Yaml(yaml));
}

// Verify EDS clusters have EDS config.
TEST_F(ClusterManagerImplTest, EdsClustersRequireEdsConfig) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_0
      type: EDS
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(create(parseBootstrapFromV2Yaml(yaml)), EnvoyException,
                            "cannot create an EDS cluster without an EDS config");
}

class ClusterManagerImplThreadAwareLbTest : public ClusterManagerImplTest {
public:
  void doTest(LoadBalancerType lb_type) {
    const std::string json =
        fmt::sprintf("{%s}", clustersJson({defaultStaticClusterJson("cluster_0")}));

    std::shared_ptr<MockCluster> cluster1(new NiceMock<MockCluster>());
    cluster1->info_->name_ = "cluster_0";
    cluster1->info_->lb_type_ = lb_type;

    InSequence s;
    EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _, _)).WillOnce(Return(cluster1));
    ON_CALL(*cluster1, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
    create(parseBootstrapFromJson(json));

    EXPECT_EQ(nullptr, cluster_manager_->get("cluster_0")->loadBalancer().chooseHost(nullptr));

    cluster1->prioritySet().getMockHostSet(0)->hosts_ = {
        makeTestHost(cluster1->info_, "tcp://127.0.0.1:80")};
    cluster1->prioritySet().getMockHostSet(0)->runCallbacks(
        cluster1->prioritySet().getMockHostSet(0)->hosts_, {});
    cluster1->initialize_callback_();
    EXPECT_EQ(cluster1->prioritySet().getMockHostSet(0)->hosts_[0],
              cluster_manager_->get("cluster_0")->loadBalancer().chooseHost(nullptr));
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
  const std::string json = R"EOF(
  {
    "clusters": [
    {
      "name": "cluster_1",
      "connect_timeout_ms": 250,
      "type": "static",
      "lb_type": "round_robin",
      "hosts": [{"url": "tcp://127.0.0.1:11001"}],
      "health_check": {
        "type": "tcp",
        "timeout_ms": 1000,
        "interval_ms": 1000,
        "unhealthy_threshold": 2,
        "healthy_threshold": 2,
        "send": [
          {"binary": "01"}
        ],
        "receive": [
          {"binary": "02"}
        ]
      }
    }]
  }
  )EOF";

  Network::MockClientConnection* connection = new NiceMock<Network::MockClientConnection>();
  EXPECT_CALL(factory_.dispatcher_,
              createClientConnection_(
                  PointeesEq(Network::Utility::resolveUrl("tcp://127.0.0.1:11001")), _, _, _))
      .WillOnce(Return(connection));
  create(parseBootstrapFromJson(json));
  factory_.tls_.shutdownThread();
}

TEST_F(ClusterManagerImplTest, HttpHealthChecker) {
  const std::string json = R"EOF(
  {
    "clusters": [
    {
      "name": "cluster_1",
      "connect_timeout_ms": 250,
      "type": "static",
      "lb_type": "round_robin",
      "hosts": [{"url": "tcp://127.0.0.1:11001"}],
      "health_check": {
        "type": "http",
        "timeout_ms": 1000,
        "interval_ms": 1000,
        "unhealthy_threshold": 2,
        "healthy_threshold": 2,
        "path": "/healthcheck"
      }
    }]
  }
  )EOF";

  Network::MockClientConnection* connection = new NiceMock<Network::MockClientConnection>();
  EXPECT_CALL(factory_.dispatcher_,
              createClientConnection_(
                  PointeesEq(Network::Utility::resolveUrl("tcp://127.0.0.1:11001")), _, _, _))
      .WillOnce(Return(connection));
  create(parseBootstrapFromJson(json));
  factory_.tls_.shutdownThread();
}

TEST_F(ClusterManagerImplTest, UnknownCluster) {
  const std::string json =
      fmt::sprintf("{%s}", clustersJson({defaultStaticClusterJson("cluster_1")}));

  create(parseBootstrapFromJson(json));
  EXPECT_EQ(nullptr, cluster_manager_->get("hello"));
  EXPECT_EQ(nullptr, cluster_manager_->httpConnPoolForCluster("hello", ResourcePriority::Default,
                                                              Http::Protocol::Http2, nullptr));
  Network::TransportSocketOptionsSharedPtr transport_socket_options;
  EXPECT_EQ(nullptr, cluster_manager_->tcpConnPoolForCluster("hello", ResourcePriority::Default,
                                                             nullptr, transport_socket_options));
  EXPECT_THROW(cluster_manager_->tcpConnForCluster("hello", nullptr, transport_socket_options),
               EnvoyException);

  transport_socket_options = std::make_shared<Network::TransportSocketOptionsImpl>("example.com");
  EXPECT_EQ(nullptr, cluster_manager_->tcpConnPoolForCluster("hello", ResourcePriority::Default,
                                                             nullptr, transport_socket_options));
  EXPECT_THROW(cluster_manager_->tcpConnForCluster("hello", nullptr, transport_socket_options),
               EnvoyException);

  EXPECT_THROW(cluster_manager_->httpAsyncClientForCluster("hello"), EnvoyException);
  factory_.tls_.shutdownThread();
}

/**
 * Test that buffer limits are set on new TCP connections.
 */
TEST_F(ClusterManagerImplTest, VerifyBufferLimits) {
  const std::string json = R"EOF(
  {
    "clusters": [
    {
      "name": "cluster_1",
      "connect_timeout_ms": 250,
      "per_connection_buffer_limit_bytes": 8192,
      "type": "static",
      "lb_type": "round_robin",
      "hosts": [{"url": "tcp://127.0.0.1:11001"}]
    }]
  }
  )EOF";

  create(parseBootstrapFromJson(json));
  Network::MockClientConnection* connection = new NiceMock<Network::MockClientConnection>();
  EXPECT_CALL(*connection, setBufferLimits(8192));
  EXPECT_CALL(factory_.tls_.dispatcher_, createClientConnection_(_, _, _, _))
      .WillOnce(Return(connection));
  auto conn_data = cluster_manager_->tcpConnForCluster("cluster_1", nullptr, nullptr);
  EXPECT_EQ(connection, conn_data.connection_.get());
  factory_.tls_.shutdownThread();
}

TEST_F(ClusterManagerImplTest, ShutdownOrder) {
  const std::string json =
      fmt::sprintf("{%s}", clustersJson({defaultStaticClusterJson("cluster_1")}));

  create(parseBootstrapFromJson(json));
  const Cluster& cluster = cluster_manager_->clusters().begin()->second;
  EXPECT_EQ("cluster_1", cluster.info()->name());
  EXPECT_EQ(cluster.info(), cluster_manager_->get("cluster_1")->info());
  EXPECT_EQ(
      1UL,
      cluster_manager_->get("cluster_1")->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0],
            cluster_manager_->get("cluster_1")->loadBalancer().chooseHost(nullptr));

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
    "cds": {"cluster": %s},
    %s
  }
  )EOF",
      defaultStaticClusterJson("cds_cluster"),
      clustersJson(
          {defaultStaticClusterJson("fake_cluster"), defaultStaticClusterJson("fake_cluster2")}));

  MockCdsApi* cds = new MockCdsApi();
  std::shared_ptr<MockCluster> cds_cluster(new NiceMock<MockCluster>());
  cds_cluster->info_->name_ = "cds_cluster";
  std::shared_ptr<MockCluster> cluster1(new NiceMock<MockCluster>());
  std::shared_ptr<MockCluster> cluster2(new NiceMock<MockCluster>());
  cluster2->info_->name_ = "fake_cluster2";
  cluster2->info_->lb_type_ = LoadBalancerType::RingHash;

  // This part tests static init.
  InSequence s;
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _, _)).WillOnce(Return(cds_cluster));
  ON_CALL(*cds_cluster, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _, _)).WillOnce(Return(cluster1));
  ON_CALL(*cluster1, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _, _)).WillOnce(Return(cluster2));
  ON_CALL(*cluster2, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Secondary));
  EXPECT_CALL(factory_, createCds_()).WillOnce(Return(cds));
  EXPECT_CALL(*cds, setInitializedCb(_));
  EXPECT_CALL(*cds_cluster, initialize(_));
  EXPECT_CALL(*cluster1, initialize(_));

  create(parseBootstrapFromJson(json));

  ReadyWatcher initialized;
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

  EXPECT_CALL(*cluster2, initialize(_));
  cds_cluster->initialize_callback_();
  cluster1->initialize_callback_();

  EXPECT_CALL(*cds, initialize());
  cluster2->initialize_callback_();

  // This part tests CDS init.
  std::shared_ptr<MockCluster> cluster3(new NiceMock<MockCluster>());
  cluster3->info_->name_ = "cluster3";
  std::shared_ptr<MockCluster> cluster4(new NiceMock<MockCluster>());
  cluster4->info_->name_ = "cluster4";
  std::shared_ptr<MockCluster> cluster5(new NiceMock<MockCluster>());
  cluster5->info_->name_ = "cluster5";

  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _, _)).WillOnce(Return(cluster3));
  ON_CALL(*cluster3, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Secondary));
  cluster_manager_->addOrUpdateCluster(defaultStaticCluster("cluster3"), "version1");

  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _, _)).WillOnce(Return(cluster4));
  ON_CALL(*cluster4, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(*cluster4, initialize(_));
  cluster_manager_->addOrUpdateCluster(defaultStaticCluster("cluster4"), "version2");

  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _, _)).WillOnce(Return(cluster5));
  ON_CALL(*cluster5, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Secondary));
  cluster_manager_->addOrUpdateCluster(defaultStaticCluster("cluster5"), "version3");

  cds->initialized_callback_();
  EXPECT_CALL(*cds, versionInfo()).WillOnce(Return("version3"));
  checkConfigDump(R"EOF(
version_info: version3
static_clusters:
  - cluster:
      name: "cds_cluster"
      connect_timeout: 0.25s
      hosts:
      - socket_address:
          address: "127.0.0.1"
          port_value: 11001
      dns_lookup_family: V4_ONLY
    last_updated:
      seconds: 1234567891
      nanos: 234000000
  - cluster:
      name: "fake_cluster"
      connect_timeout: 0.25s
      hosts:
      - socket_address:
          address: "127.0.0.1"
          port_value: 11001
      dns_lookup_family: V4_ONLY
    last_updated:
      seconds: 1234567891
      nanos: 234000000
  - cluster:
      name: "fake_cluster2"
      connect_timeout: 0.25s
      hosts:
      - socket_address:
          address: "127.0.0.1"
          port_value: 11001
      dns_lookup_family: V4_ONLY
    last_updated:
      seconds: 1234567891
      nanos: 234000000
dynamic_active_clusters:
  - version_info: "version1"
    cluster:
      name: "cluster3"
      connect_timeout: 0.25s
      hosts:
      - socket_address:
          address: "127.0.0.1"
          port_value: 11001
      dns_lookup_family: V4_ONLY
    last_updated:
      seconds: 1234567891
      nanos: 234000000
  - version_info: "version2"
    cluster:
      name: "cluster4"
      connect_timeout: 0.25s
      hosts:
      - socket_address:
          address: "127.0.0.1"
          port_value: 11001
      dns_lookup_family: V4_ONLY
    last_updated:
      seconds: 1234567891
      nanos: 234000000
  - version_info: "version3"
    cluster:
      name: "cluster5"
      connect_timeout: 0.25s
      hosts:
      - socket_address:
          address: "127.0.0.1"
          port_value: 11001
      dns_lookup_family: V4_ONLY
    last_updated:
      seconds: 1234567891
      nanos: 234000000
dynamic_warming_clusters:
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
    "local_cluster_name": "foo",
    %s
  }
  )EOF",
                                        clustersJson({defaultStaticClusterJson("fake")}));

  std::shared_ptr<MockCluster> foo(new NiceMock<MockCluster>());
  foo->info_->name_ = "foo";
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _, false)).WillOnce(Return(foo));
  ON_CALL(*foo, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(*foo, initialize(_));

  create(parseBootstrapFromJson(json));
  foo->initialize_callback_();

  // Now add a dynamic cluster. This cluster will have a member update callback from the local
  // cluster in its load balancer.
  std::shared_ptr<MockCluster> cluster1(new NiceMock<MockCluster>());
  cluster1->info_->name_ = "cluster1";
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _, true)).WillOnce(Return(cluster1));
  ON_CALL(*cluster1, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(*cluster1, initialize(_));
  cluster_manager_->addOrUpdateCluster(defaultStaticCluster("cluster1"), "");

  // Add another update callback on foo so we make sure callbacks keep working.
  ReadyWatcher membership_updated;
  foo->prioritySet().addMemberUpdateCb(
      [&membership_updated](uint32_t, const HostVector&, const HostVector&) -> void {
        membership_updated.ready();
      });

  // Remove the new cluster.
  cluster_manager_->removeCluster("cluster1");

  // Fire a member callback on the local cluster, which should not call any update callbacks on
  // the deleted cluster.
  foo->prioritySet().getMockHostSet(0)->hosts_ = {makeTestHost(foo->info_, "tcp://127.0.0.1:80")};
  EXPECT_CALL(membership_updated, ready());
  foo->prioritySet().getMockHostSet(0)->runCallbacks(foo->prioritySet().getMockHostSet(0)->hosts_,
                                                     {});

  factory_.tls_.shutdownThread();

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(foo.get()));
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster1.get()));
}

TEST_F(ClusterManagerImplTest, RemoveWarmingCluster) {
  time_system_.setSystemTime(std::chrono::milliseconds(1234567891234));

  const std::string json = R"EOF(
  {
    "clusters": []
  }
  )EOF";

  create(parseBootstrapFromJson(json));

  InSequence s;
  ReadyWatcher initialized;
  EXPECT_CALL(initialized, ready());
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

  std::shared_ptr<MockCluster> cluster1(new NiceMock<MockCluster>());
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _, _)).WillOnce(Return(cluster1));
  EXPECT_CALL(*cluster1, initializePhase()).Times(0);
  EXPECT_CALL(*cluster1, initialize(_));
  EXPECT_TRUE(
      cluster_manager_->addOrUpdateCluster(defaultStaticCluster("fake_cluster"), "version1"));
  checkStats(1 /*added*/, 0 /*modified*/, 0 /*removed*/, 0 /*active*/, 1 /*warming*/);
  EXPECT_EQ(nullptr, cluster_manager_->get("fake_cluster"));
  checkConfigDump(R"EOF(
dynamic_warming_clusters:
  - version_info: "version1"
    cluster:
      name: "fake_cluster"
      connect_timeout: 0.25s
      hosts:
      - socket_address:
          address: "127.0.0.1"
          port_value: 11001
      dns_lookup_family: V4_ONLY
    last_updated:
      seconds: 1234567891
      nanos: 234000000
)EOF");

  EXPECT_TRUE(cluster_manager_->removeCluster("fake_cluster"));
  checkStats(1 /*added*/, 0 /*modified*/, 1 /*removed*/, 0 /*active*/, 0 /*warming*/);

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster1.get()));
}

TEST_F(ClusterManagerImplTest, DynamicAddRemove) {
  const std::string json = R"EOF(
  {
    "clusters": []
  }
  )EOF";

  create(parseBootstrapFromJson(json));

  InSequence s;
  ReadyWatcher initialized;
  EXPECT_CALL(initialized, ready());
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

  std::unique_ptr<MockClusterUpdateCallbacks> callbacks(new NiceMock<MockClusterUpdateCallbacks>());
  ClusterUpdateCallbacksHandlePtr cb =
      cluster_manager_->addThreadLocalClusterUpdateCallbacks(*callbacks);

  std::shared_ptr<MockCluster> cluster1(new NiceMock<MockCluster>());
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _, _)).WillOnce(Return(cluster1));
  EXPECT_CALL(*cluster1, initializePhase()).Times(0);
  EXPECT_CALL(*cluster1, initialize(_));
  EXPECT_CALL(*callbacks, onClusterAddOrUpdate(_)).Times(1);
  EXPECT_TRUE(cluster_manager_->addOrUpdateCluster(defaultStaticCluster("fake_cluster"), ""));
  checkStats(1 /*added*/, 0 /*modified*/, 0 /*removed*/, 0 /*active*/, 1 /*warming*/);
  EXPECT_EQ(nullptr, cluster_manager_->get("fake_cluster"));
  cluster1->initialize_callback_();

  EXPECT_EQ(cluster1->info_, cluster_manager_->get("fake_cluster")->info());
  checkStats(1 /*added*/, 0 /*modified*/, 0 /*removed*/, 1 /*active*/, 0 /*warming*/);

  // Now try to update again but with the same hash.
  EXPECT_FALSE(cluster_manager_->addOrUpdateCluster(defaultStaticCluster("fake_cluster"), ""));

  // Now do it again with a different hash.
  auto update_cluster = defaultStaticCluster("fake_cluster");
  update_cluster.mutable_per_connection_buffer_limit_bytes()->set_value(12345);

  std::shared_ptr<MockCluster> cluster2(new NiceMock<MockCluster>());
  cluster2->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster2->info_, "tcp://127.0.0.1:80")};
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _, _)).WillOnce(Return(cluster2));
  EXPECT_CALL(*cluster2, initializePhase()).Times(0);
  EXPECT_CALL(*cluster2, initialize(_))
      .WillOnce(Invoke([cluster1](std::function<void()> initialize_callback) {
        // Test inline init.
        initialize_callback();
      }));
  EXPECT_CALL(*callbacks, onClusterAddOrUpdate(_)).Times(1);
  EXPECT_TRUE(cluster_manager_->addOrUpdateCluster(update_cluster, ""));

  EXPECT_EQ(cluster2->info_, cluster_manager_->get("fake_cluster")->info());
  EXPECT_EQ(1UL, cluster_manager_->clusters().size());
  Http::ConnectionPool::MockInstance* cp = new Http::ConnectionPool::MockInstance();
  EXPECT_CALL(factory_, allocateConnPool_(_)).WillOnce(Return(cp));
  EXPECT_EQ(cp, cluster_manager_->httpConnPoolForCluster("fake_cluster", ResourcePriority::Default,
                                                         Http::Protocol::Http11, nullptr));

  Tcp::ConnectionPool::MockInstance* cp2 = new Tcp::ConnectionPool::MockInstance();
  EXPECT_CALL(factory_, allocateTcpConnPool_(_)).WillOnce(Return(cp2));
  EXPECT_EQ(cp2, cluster_manager_->tcpConnPoolForCluster("fake_cluster", ResourcePriority::Default,
                                                         nullptr, nullptr));

  Network::MockClientConnection* connection = new Network::MockClientConnection();
  ON_CALL(*cluster2->info_, features())
      .WillByDefault(Return(ClusterInfo::Features::CLOSE_CONNECTIONS_ON_HOST_HEALTH_FAILURE));
  EXPECT_CALL(factory_.tls_.dispatcher_, createClientConnection_(_, _, _, _))
      .WillOnce(Return(connection));
  EXPECT_CALL(*connection, setBufferLimits(_));
  EXPECT_CALL(*connection, addConnectionCallbacks(_));
  auto conn_info = cluster_manager_->tcpConnForCluster("fake_cluster", nullptr, nullptr);
  EXPECT_EQ(conn_info.connection_.get(), connection);

  // Now remove the cluster. This should drain the connection pools, but not affect
  // tcp connections.
  Http::ConnectionPool::Instance::DrainedCb drained_cb;
  Tcp::ConnectionPool::Instance::DrainedCb drained_cb2;
  EXPECT_CALL(*cp, addDrainedCallback(_)).WillOnce(SaveArg<0>(&drained_cb));
  EXPECT_CALL(*cp2, addDrainedCallback(_)).WillOnce(SaveArg<0>(&drained_cb2));
  EXPECT_CALL(*callbacks, onClusterRemoval(_)).Times(1);
  EXPECT_TRUE(cluster_manager_->removeCluster("fake_cluster"));
  EXPECT_EQ(nullptr, cluster_manager_->get("fake_cluster"));
  EXPECT_EQ(0UL, cluster_manager_->clusters().size());

  // Close the TCP connection. Success is no ASSERT or crash due to referencing
  // the removed cluster.
  EXPECT_CALL(*connection, dispatcher());
  connection->raiseEvent(Network::ConnectionEvent::LocalClose);

  // Remove an unknown cluster.
  EXPECT_FALSE(cluster_manager_->removeCluster("foo"));

  drained_cb();
  drained_cb2();

  checkStats(1 /*added*/, 1 /*modified*/, 1 /*removed*/, 0 /*active*/, 0 /*warming*/);

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster1.get()));
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster2.get()));
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(callbacks.get()));
}

TEST_F(ClusterManagerImplTest, addOrUpdateClusterStaticExists) {
  const std::string json =
      fmt::sprintf("{%s}", clustersJson({defaultStaticClusterJson("some_cluster")}));
  std::shared_ptr<MockCluster> cluster1(new NiceMock<MockCluster>());
  InSequence s;
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _, _)).WillOnce(Return(cluster1));
  ON_CALL(*cluster1, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(*cluster1, initialize(_));

  create(parseBootstrapFromJson(json));

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

// Test that we close all HTTP connection pool connections when there is a host health failure.
TEST_F(ClusterManagerImplTest, CloseHttpConnectionsOnHealthFailure) {
  const std::string json =
      fmt::sprintf("{%s}", clustersJson({defaultStaticClusterJson("some_cluster")}));
  std::shared_ptr<MockCluster> cluster1(new NiceMock<MockCluster>());
  cluster1->info_->name_ = "some_cluster";
  HostSharedPtr test_host = makeTestHost(cluster1->info_, "tcp://127.0.0.1:80");
  cluster1->prioritySet().getMockHostSet(0)->hosts_ = {test_host};
  ON_CALL(*cluster1, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));

  MockHealthChecker health_checker;
  ON_CALL(*cluster1, healthChecker()).WillByDefault(Return(&health_checker));

  Outlier::MockDetector outlier_detector;
  ON_CALL(*cluster1, outlierDetector()).WillByDefault(Return(&outlier_detector));

  Http::ConnectionPool::MockInstance* cp1 = new Http::ConnectionPool::MockInstance();
  Http::ConnectionPool::MockInstance* cp2 = new Http::ConnectionPool::MockInstance();

  {
    InSequence s;

    EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _, _)).WillOnce(Return(cluster1));
    EXPECT_CALL(health_checker, addHostCheckCompleteCb(_));
    EXPECT_CALL(outlier_detector, addChangedStateCb(_));
    EXPECT_CALL(*cluster1, initialize(_))
        .WillOnce(Invoke([cluster1](std::function<void()> initialize_callback) {
          // Test inline init.
          initialize_callback();
        }));
    create(parseBootstrapFromJson(json));

    EXPECT_CALL(factory_, allocateConnPool_(_)).WillOnce(Return(cp1));
    cluster_manager_->httpConnPoolForCluster("some_cluster", ResourcePriority::Default,
                                             Http::Protocol::Http11, nullptr);

    outlier_detector.runCallbacks(test_host);
    health_checker.runCallbacks(test_host, HealthTransition::Unchanged);

    EXPECT_CALL(*cp1, drainConnections());
    test_host->healthFlagSet(Host::HealthFlag::FAILED_OUTLIER_CHECK);
    outlier_detector.runCallbacks(test_host);

    EXPECT_CALL(factory_, allocateConnPool_(_)).WillOnce(Return(cp2));
    cluster_manager_->httpConnPoolForCluster("some_cluster", ResourcePriority::High,
                                             Http::Protocol::Http11, nullptr);
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

// Test that we close all TCP connection pool connections when there is a host health failure.
TEST_F(ClusterManagerImplTest, CloseTcpConnectionPoolsOnHealthFailure) {
  const std::string json =
      fmt::sprintf("{%s}", clustersJson({defaultStaticClusterJson("some_cluster")}));
  std::shared_ptr<MockCluster> cluster1(new NiceMock<MockCluster>());
  cluster1->info_->name_ = "some_cluster";
  HostSharedPtr test_host = makeTestHost(cluster1->info_, "tcp://127.0.0.1:80");
  cluster1->prioritySet().getMockHostSet(0)->hosts_ = {test_host};
  ON_CALL(*cluster1, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));

  MockHealthChecker health_checker;
  ON_CALL(*cluster1, healthChecker()).WillByDefault(Return(&health_checker));

  Outlier::MockDetector outlier_detector;
  ON_CALL(*cluster1, outlierDetector()).WillByDefault(Return(&outlier_detector));

  Tcp::ConnectionPool::MockInstance* cp1 = new Tcp::ConnectionPool::MockInstance();
  Tcp::ConnectionPool::MockInstance* cp2 = new Tcp::ConnectionPool::MockInstance();

  {
    InSequence s;

    EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _, _)).WillOnce(Return(cluster1));
    EXPECT_CALL(health_checker, addHostCheckCompleteCb(_));
    EXPECT_CALL(outlier_detector, addChangedStateCb(_));
    EXPECT_CALL(*cluster1, initialize(_))
        .WillOnce(Invoke([cluster1](std::function<void()> initialize_callback) {
          // Test inline init.
          initialize_callback();
        }));
    create(parseBootstrapFromJson(json));

    EXPECT_CALL(factory_, allocateTcpConnPool_(_)).WillOnce(Return(cp1));
    cluster_manager_->tcpConnPoolForCluster("some_cluster", ResourcePriority::Default, nullptr,
                                            nullptr);

    outlier_detector.runCallbacks(test_host);
    health_checker.runCallbacks(test_host, HealthTransition::Unchanged);

    EXPECT_CALL(*cp1, drainConnections());
    test_host->healthFlagSet(Host::HealthFlag::FAILED_OUTLIER_CHECK);
    outlier_detector.runCallbacks(test_host);

    EXPECT_CALL(factory_, allocateTcpConnPool_(_)).WillOnce(Return(cp2));
    cluster_manager_->tcpConnPoolForCluster("some_cluster", ResourcePriority::High, nullptr,
                                            nullptr);
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

// Test that we close all TCP connection pool connections when there is a host health failure, when
// configured to do so.
TEST_F(ClusterManagerImplTest, CloseTcpConnectionsOnHealthFailure) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: some_cluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      close_connections_on_host_health_failure: true
  )EOF";
  std::shared_ptr<MockCluster> cluster1(new NiceMock<MockCluster>());
  EXPECT_CALL(*cluster1->info_, features())
      .WillRepeatedly(Return(ClusterInfo::Features::CLOSE_CONNECTIONS_ON_HOST_HEALTH_FAILURE));
  cluster1->info_->name_ = "some_cluster";
  HostSharedPtr test_host = makeTestHost(cluster1->info_, "tcp://127.0.0.1:80");
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

    EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _, _)).WillOnce(Return(cluster1));
    EXPECT_CALL(health_checker, addHostCheckCompleteCb(_));
    EXPECT_CALL(outlier_detector, addChangedStateCb(_));
    EXPECT_CALL(*cluster1, initialize(_))
        .WillOnce(Invoke([cluster1](std::function<void()> initialize_callback) {
          // Test inline init.
          initialize_callback();
        }));
    create(parseBootstrapFromV2Yaml(yaml));

    EXPECT_CALL(factory_.tls_.dispatcher_, createClientConnection_(_, _, _, _))
        .WillOnce(Return(connection1));
    conn_info1 = cluster_manager_->tcpConnForCluster("some_cluster", nullptr, nullptr);

    outlier_detector.runCallbacks(test_host);
    health_checker.runCallbacks(test_host, HealthTransition::Unchanged);

    EXPECT_CALL(*connection1, close(Network::ConnectionCloseType::NoFlush));
    test_host->healthFlagSet(Host::HealthFlag::FAILED_OUTLIER_CHECK);
    outlier_detector.runCallbacks(test_host);

    connection1 = new NiceMock<Network::MockClientConnection>();
    EXPECT_CALL(factory_.tls_.dispatcher_, createClientConnection_(_, _, _, _))
        .WillOnce(Return(connection1));
    conn_info1 = cluster_manager_->tcpConnForCluster("some_cluster", nullptr, nullptr);

    EXPECT_CALL(factory_.tls_.dispatcher_, createClientConnection_(_, _, _, _))
        .WillOnce(Return(connection2));
    conn_info2 = cluster_manager_->tcpConnForCluster("some_cluster", nullptr, nullptr);
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
  std::shared_ptr<MockCluster> cluster1(new NiceMock<MockCluster>());
  EXPECT_CALL(*cluster1->info_, features()).WillRepeatedly(Return(0));
  cluster1->info_->name_ = "some_cluster";
  HostSharedPtr test_host = makeTestHost(cluster1->info_, "tcp://127.0.0.1:80");
  cluster1->prioritySet().getMockHostSet(0)->hosts_ = {test_host};
  ON_CALL(*cluster1, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));

  MockHealthChecker health_checker;
  ON_CALL(*cluster1, healthChecker()).WillByDefault(Return(&health_checker));

  Outlier::MockDetector outlier_detector;
  ON_CALL(*cluster1, outlierDetector()).WillByDefault(Return(&outlier_detector));

  Network::MockClientConnection* connection1 = new NiceMock<Network::MockClientConnection>();
  Host::CreateConnectionData conn_info1;

  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _, _)).WillOnce(Return(cluster1));
  EXPECT_CALL(health_checker, addHostCheckCompleteCb(_));
  EXPECT_CALL(outlier_detector, addChangedStateCb(_));
  EXPECT_CALL(*cluster1, initialize(_))
      .WillOnce(Invoke([cluster1](std::function<void()> initialize_callback) {
        // Test inline init.
        initialize_callback();
      }));
  create(parseBootstrapFromV2Yaml(yaml));

  EXPECT_CALL(factory_.tls_.dispatcher_, createClientConnection_(_, _, _, _))
      .WillOnce(Return(connection1));
  conn_info1 = cluster_manager_->tcpConnForCluster("some_cluster", nullptr, nullptr);

  outlier_detector.runCallbacks(test_host);
  health_checker.runCallbacks(test_host, HealthTransition::Unchanged);

  EXPECT_CALL(*connection1, close(_)).Times(0);
  test_host->healthFlagSet(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  outlier_detector.runCallbacks(test_host);

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster1.get()));
}

TEST_F(ClusterManagerImplTest, DynamicHostRemove) {
  const std::string json = R"EOF(
  {
    "clusters": [
    {
      "name": "cluster_1",
      "connect_timeout_ms": 250,
      "type": "strict_dns",
      "dns_resolvers": [ "1.2.3.4:80" ],
      "lb_type": "round_robin",
      "hosts": [{"url": "tcp://localhost:11001"}]
    }]
  }
  )EOF";

  std::shared_ptr<Network::MockDnsResolver> dns_resolver(new Network::MockDnsResolver());
  EXPECT_CALL(factory_.dispatcher_, createDnsResolver(_)).WillOnce(Return(dns_resolver));

  Network::DnsResolver::ResolveCb dns_callback;
  Event::MockTimer* dns_timer_ = new NiceMock<Event::MockTimer>(&factory_.dispatcher_);
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromJson(json));
  EXPECT_FALSE(cluster_manager_->get("cluster_1")->info()->addedViaApi());

  // Test for no hosts returning the correct values before we have hosts.
  EXPECT_EQ(nullptr, cluster_manager_->httpConnPoolForCluster(
                         "cluster_1", ResourcePriority::Default, Http::Protocol::Http11, nullptr));
  EXPECT_EQ(nullptr, cluster_manager_->tcpConnPoolForCluster("cluster_1", ResourcePriority::Default,
                                                             nullptr, nullptr));
  EXPECT_EQ(nullptr,
            cluster_manager_->tcpConnForCluster("cluster_1", nullptr, nullptr).connection_);
  EXPECT_EQ(3UL, factory_.stats_.counter("cluster.cluster_1.upstream_cx_none_healthy").value());

  // Set up for an initialize callback.
  ReadyWatcher initialized;
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });
  EXPECT_CALL(initialized, ready());

  dns_callback(TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2"}));

  // After we are initialized, we should immediately get called back if someone asks for an
  // initialize callback.
  EXPECT_CALL(initialized, ready());
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

  EXPECT_CALL(factory_, allocateConnPool_(_))
      .Times(4)
      .WillRepeatedly(ReturnNew<Http::ConnectionPool::MockInstance>());

  // This should provide us a CP for each of the above hosts.
  Http::ConnectionPool::MockInstance* cp1 =
      dynamic_cast<Http::ConnectionPool::MockInstance*>(cluster_manager_->httpConnPoolForCluster(
          "cluster_1", ResourcePriority::Default, Http::Protocol::Http11, nullptr));
  Http::ConnectionPool::MockInstance* cp2 =
      dynamic_cast<Http::ConnectionPool::MockInstance*>(cluster_manager_->httpConnPoolForCluster(
          "cluster_1", ResourcePriority::Default, Http::Protocol::Http11, nullptr));
  Http::ConnectionPool::MockInstance* cp1_high =
      dynamic_cast<Http::ConnectionPool::MockInstance*>(cluster_manager_->httpConnPoolForCluster(
          "cluster_1", ResourcePriority::High, Http::Protocol::Http11, nullptr));
  Http::ConnectionPool::MockInstance* cp2_high =
      dynamic_cast<Http::ConnectionPool::MockInstance*>(cluster_manager_->httpConnPoolForCluster(
          "cluster_1", ResourcePriority::High, Http::Protocol::Http11, nullptr));

  EXPECT_NE(cp1, cp2);
  EXPECT_NE(cp1_high, cp2_high);
  EXPECT_NE(cp1, cp1_high);

  Http::ConnectionPool::Instance::DrainedCb drained_cb;
  EXPECT_CALL(*cp1, addDrainedCallback(_)).WillOnce(SaveArg<0>(&drained_cb));
  Http::ConnectionPool::Instance::DrainedCb drained_cb_high;
  EXPECT_CALL(*cp1_high, addDrainedCallback(_)).WillOnce(SaveArg<0>(&drained_cb_high));

  EXPECT_CALL(factory_, allocateTcpConnPool_(_))
      .Times(4)
      .WillRepeatedly(ReturnNew<Tcp::ConnectionPool::MockInstance>());

  // This should provide us a CP for each of the above hosts.
  Tcp::ConnectionPool::MockInstance* tcp1 =
      dynamic_cast<Tcp::ConnectionPool::MockInstance*>(cluster_manager_->tcpConnPoolForCluster(
          "cluster_1", ResourcePriority::Default, nullptr, nullptr));
  Tcp::ConnectionPool::MockInstance* tcp2 =
      dynamic_cast<Tcp::ConnectionPool::MockInstance*>(cluster_manager_->tcpConnPoolForCluster(
          "cluster_1", ResourcePriority::Default, nullptr, nullptr));
  Tcp::ConnectionPool::MockInstance* tcp1_high =
      dynamic_cast<Tcp::ConnectionPool::MockInstance*>(cluster_manager_->tcpConnPoolForCluster(
          "cluster_1", ResourcePriority::High, nullptr, nullptr));
  Tcp::ConnectionPool::MockInstance* tcp2_high =
      dynamic_cast<Tcp::ConnectionPool::MockInstance*>(cluster_manager_->tcpConnPoolForCluster(
          "cluster_1", ResourcePriority::High, nullptr, nullptr));

  EXPECT_NE(tcp1, tcp2);
  EXPECT_NE(tcp1_high, tcp2_high);
  EXPECT_NE(tcp1, tcp1_high);

  Tcp::ConnectionPool::Instance::DrainedCb tcp_drained_cb;
  EXPECT_CALL(*tcp1, addDrainedCallback(_)).WillOnce(SaveArg<0>(&tcp_drained_cb));
  Tcp::ConnectionPool::Instance::DrainedCb tcp_drained_cb_high;
  EXPECT_CALL(*tcp1_high, addDrainedCallback(_)).WillOnce(SaveArg<0>(&tcp_drained_cb_high));

  // Remove the first host, this should lead to the first cp being drained.
  dns_timer_->callback_();
  dns_callback(TestUtility::makeDnsResponse({"127.0.0.2"}));
  drained_cb();
  drained_cb = nullptr;
  tcp_drained_cb();
  tcp_drained_cb = nullptr;
  EXPECT_CALL(factory_.tls_.dispatcher_, deferredDelete_(_)).Times(4);
  drained_cb_high();
  drained_cb_high = nullptr;
  tcp_drained_cb_high();
  tcp_drained_cb_high = nullptr;

  // Make sure we get back the same connection pool for the 2nd host as we did before the change.
  Http::ConnectionPool::MockInstance* cp3 =
      dynamic_cast<Http::ConnectionPool::MockInstance*>(cluster_manager_->httpConnPoolForCluster(
          "cluster_1", ResourcePriority::Default, Http::Protocol::Http11, nullptr));
  Http::ConnectionPool::MockInstance* cp3_high =
      dynamic_cast<Http::ConnectionPool::MockInstance*>(cluster_manager_->httpConnPoolForCluster(
          "cluster_1", ResourcePriority::High, Http::Protocol::Http11, nullptr));
  EXPECT_EQ(cp2, cp3);
  EXPECT_EQ(cp2_high, cp3_high);

  Tcp::ConnectionPool::MockInstance* tcp3 =
      dynamic_cast<Tcp::ConnectionPool::MockInstance*>(cluster_manager_->tcpConnPoolForCluster(
          "cluster_1", ResourcePriority::Default, nullptr, nullptr));
  Tcp::ConnectionPool::MockInstance* tcp3_high =
      dynamic_cast<Tcp::ConnectionPool::MockInstance*>(cluster_manager_->tcpConnPoolForCluster(
          "cluster_1", ResourcePriority::High, nullptr, nullptr));
  EXPECT_EQ(tcp2, tcp3);
  EXPECT_EQ(tcp2_high, tcp3_high);

  // Now add and remove a host that we never have a conn pool to. This should not lead to any
  // drain callbacks, etc.
  dns_timer_->callback_();
  dns_callback(TestUtility::makeDnsResponse({"127.0.0.2", "127.0.0.3"}));

  factory_.tls_.shutdownThread();
}

TEST_F(ClusterManagerImplTest, DynamicHostRemoveWithTls) {
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
      lb_policy: ROUND_ROBIN
      hosts:
      - socket_address:
          address: localhost
          port_value: 11001
  )EOF";

  std::shared_ptr<Network::MockDnsResolver> dns_resolver(new Network::MockDnsResolver());
  EXPECT_CALL(factory_.dispatcher_, createDnsResolver(_)).WillOnce(Return(dns_resolver));

  Network::DnsResolver::ResolveCb dns_callback;
  Event::MockTimer* dns_timer_ = new NiceMock<Event::MockTimer>(&factory_.dispatcher_);
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV2Yaml(yaml));
  EXPECT_FALSE(cluster_manager_->get("cluster_1")->info()->addedViaApi());

  Network::TransportSocketOptionsSharedPtr transport_socket_options_example_com(
      new Network::TransportSocketOptionsImpl("example.com"));
  Network::TransportSocketOptionsSharedPtr transport_socket_options_ibm_com(
      new Network::TransportSocketOptionsImpl("ibm.com"));

  // Test for no hosts returning the correct values before we have hosts.
  EXPECT_EQ(nullptr, cluster_manager_->httpConnPoolForCluster(
                         "cluster_1", ResourcePriority::Default, Http::Protocol::Http11, nullptr));
  EXPECT_EQ(nullptr, cluster_manager_->tcpConnPoolForCluster("cluster_1", ResourcePriority::Default,
                                                             nullptr, nullptr));
  EXPECT_EQ(nullptr,
            cluster_manager_->tcpConnForCluster("cluster_1", nullptr, nullptr).connection_);

  EXPECT_EQ(nullptr,
            cluster_manager_->tcpConnPoolForCluster("cluster_1", ResourcePriority::Default, nullptr,
                                                    transport_socket_options_example_com));
  EXPECT_EQ(nullptr,
            cluster_manager_
                ->tcpConnForCluster("cluster_1", nullptr, transport_socket_options_example_com)
                .connection_);

  EXPECT_EQ(nullptr,
            cluster_manager_->tcpConnPoolForCluster("cluster_1", ResourcePriority::Default, nullptr,
                                                    transport_socket_options_ibm_com));
  EXPECT_EQ(nullptr, cluster_manager_
                         ->tcpConnForCluster("cluster_1", nullptr, transport_socket_options_ibm_com)
                         .connection_);

  EXPECT_EQ(7UL, factory_.stats_.counter("cluster.cluster_1.upstream_cx_none_healthy").value());

  // Set up for an initialize callback.
  ReadyWatcher initialized;
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });
  EXPECT_CALL(initialized, ready());

  dns_callback(TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2"}));

  // After we are initialized, we should immediately get called back if someone asks for an
  // initialize callback.
  EXPECT_CALL(initialized, ready());
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

  EXPECT_CALL(factory_, allocateConnPool_(_))
      .Times(4)
      .WillRepeatedly(ReturnNew<Http::ConnectionPool::MockInstance>());

  // This should provide us a CP for each of the above hosts.
  Http::ConnectionPool::MockInstance* cp1 =
      dynamic_cast<Http::ConnectionPool::MockInstance*>(cluster_manager_->httpConnPoolForCluster(
          "cluster_1", ResourcePriority::Default, Http::Protocol::Http11, nullptr));
  Http::ConnectionPool::MockInstance* cp2 =
      dynamic_cast<Http::ConnectionPool::MockInstance*>(cluster_manager_->httpConnPoolForCluster(
          "cluster_1", ResourcePriority::Default, Http::Protocol::Http11, nullptr));
  Http::ConnectionPool::MockInstance* cp1_high =
      dynamic_cast<Http::ConnectionPool::MockInstance*>(cluster_manager_->httpConnPoolForCluster(
          "cluster_1", ResourcePriority::High, Http::Protocol::Http11, nullptr));
  Http::ConnectionPool::MockInstance* cp2_high =
      dynamic_cast<Http::ConnectionPool::MockInstance*>(cluster_manager_->httpConnPoolForCluster(
          "cluster_1", ResourcePriority::High, Http::Protocol::Http11, nullptr));

  EXPECT_NE(cp1, cp2);
  EXPECT_NE(cp1_high, cp2_high);
  EXPECT_NE(cp1, cp1_high);

  Http::ConnectionPool::Instance::DrainedCb drained_cb;
  EXPECT_CALL(*cp1, addDrainedCallback(_)).WillOnce(SaveArg<0>(&drained_cb));
  Http::ConnectionPool::Instance::DrainedCb drained_cb_high;
  EXPECT_CALL(*cp1_high, addDrainedCallback(_)).WillOnce(SaveArg<0>(&drained_cb_high));

  EXPECT_CALL(factory_, allocateTcpConnPool_(_))
      .Times(8)
      .WillRepeatedly(ReturnNew<Tcp::ConnectionPool::MockInstance>());

  // This should provide us a CP for each of the above hosts, and for different SNIs
  Tcp::ConnectionPool::MockInstance* tcp1 =
      dynamic_cast<Tcp::ConnectionPool::MockInstance*>(cluster_manager_->tcpConnPoolForCluster(
          "cluster_1", ResourcePriority::Default, nullptr, nullptr));
  Tcp::ConnectionPool::MockInstance* tcp2 =
      dynamic_cast<Tcp::ConnectionPool::MockInstance*>(cluster_manager_->tcpConnPoolForCluster(
          "cluster_1", ResourcePriority::Default, nullptr, nullptr));
  Tcp::ConnectionPool::MockInstance* tcp1_high =
      dynamic_cast<Tcp::ConnectionPool::MockInstance*>(cluster_manager_->tcpConnPoolForCluster(
          "cluster_1", ResourcePriority::High, nullptr, nullptr));
  Tcp::ConnectionPool::MockInstance* tcp2_high =
      dynamic_cast<Tcp::ConnectionPool::MockInstance*>(cluster_manager_->tcpConnPoolForCluster(
          "cluster_1", ResourcePriority::High, nullptr, nullptr));

  Tcp::ConnectionPool::MockInstance* tcp1_example_com =
      dynamic_cast<Tcp::ConnectionPool::MockInstance*>(cluster_manager_->tcpConnPoolForCluster(
          "cluster_1", ResourcePriority::Default, nullptr, transport_socket_options_example_com));
  Tcp::ConnectionPool::MockInstance* tcp2_example_com =
      dynamic_cast<Tcp::ConnectionPool::MockInstance*>(cluster_manager_->tcpConnPoolForCluster(
          "cluster_1", ResourcePriority::Default, nullptr, transport_socket_options_example_com));

  Tcp::ConnectionPool::MockInstance* tcp1_ibm_com =
      dynamic_cast<Tcp::ConnectionPool::MockInstance*>(cluster_manager_->tcpConnPoolForCluster(
          "cluster_1", ResourcePriority::Default, nullptr, transport_socket_options_ibm_com));
  Tcp::ConnectionPool::MockInstance* tcp2_ibm_com =
      dynamic_cast<Tcp::ConnectionPool::MockInstance*>(cluster_manager_->tcpConnPoolForCluster(
          "cluster_1", ResourcePriority::Default, nullptr, transport_socket_options_ibm_com));

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

  Tcp::ConnectionPool::Instance::DrainedCb tcp_drained_cb;
  EXPECT_CALL(*tcp1, addDrainedCallback(_)).WillOnce(SaveArg<0>(&tcp_drained_cb));
  Tcp::ConnectionPool::Instance::DrainedCb tcp_drained_cb_high;
  EXPECT_CALL(*tcp1_high, addDrainedCallback(_)).WillOnce(SaveArg<0>(&tcp_drained_cb_high));

  Tcp::ConnectionPool::Instance::DrainedCb tcp_drained_cb_example_com;
  EXPECT_CALL(*tcp1_example_com, addDrainedCallback(_))
      .WillOnce(SaveArg<0>(&tcp_drained_cb_example_com));
  Tcp::ConnectionPool::Instance::DrainedCb tcp_drained_cb_ibm_com;
  EXPECT_CALL(*tcp1_ibm_com, addDrainedCallback(_)).WillOnce(SaveArg<0>(&tcp_drained_cb_ibm_com));

  // Remove the first host, this should lead to the first cp being drained.
  dns_timer_->callback_();
  dns_callback(TestUtility::makeDnsResponse({"127.0.0.2"}));
  drained_cb();
  drained_cb = nullptr;
  tcp_drained_cb();
  tcp_drained_cb = nullptr;
  drained_cb_high();
  drained_cb_high = nullptr;
  tcp_drained_cb_high();
  tcp_drained_cb_high = nullptr;
  tcp_drained_cb_example_com();
  tcp_drained_cb_example_com = nullptr;
  tcp_drained_cb_ibm_com();
  tcp_drained_cb_ibm_com = nullptr;

  // Make sure we get back the same connection pool for the 2nd host as we did before the change.
  Http::ConnectionPool::MockInstance* cp3 =
      dynamic_cast<Http::ConnectionPool::MockInstance*>(cluster_manager_->httpConnPoolForCluster(
          "cluster_1", ResourcePriority::Default, Http::Protocol::Http11, nullptr));
  Http::ConnectionPool::MockInstance* cp3_high =
      dynamic_cast<Http::ConnectionPool::MockInstance*>(cluster_manager_->httpConnPoolForCluster(
          "cluster_1", ResourcePriority::High, Http::Protocol::Http11, nullptr));
  EXPECT_EQ(cp2, cp3);
  EXPECT_EQ(cp2_high, cp3_high);

  Tcp::ConnectionPool::MockInstance* tcp3 =
      dynamic_cast<Tcp::ConnectionPool::MockInstance*>(cluster_manager_->tcpConnPoolForCluster(
          "cluster_1", ResourcePriority::Default, nullptr, nullptr));
  Tcp::ConnectionPool::MockInstance* tcp3_high =
      dynamic_cast<Tcp::ConnectionPool::MockInstance*>(cluster_manager_->tcpConnPoolForCluster(
          "cluster_1", ResourcePriority::High, nullptr, nullptr));

  Tcp::ConnectionPool::MockInstance* tcp3_example_com =
      dynamic_cast<Tcp::ConnectionPool::MockInstance*>(cluster_manager_->tcpConnPoolForCluster(
          "cluster_1", ResourcePriority::Default, nullptr, transport_socket_options_example_com));
  Tcp::ConnectionPool::MockInstance* tcp3_ibm_com =
      dynamic_cast<Tcp::ConnectionPool::MockInstance*>(cluster_manager_->tcpConnPoolForCluster(
          "cluster_1", ResourcePriority::Default, nullptr, transport_socket_options_ibm_com));

  EXPECT_EQ(tcp2, tcp3);
  EXPECT_EQ(tcp2_high, tcp3_high);

  EXPECT_EQ(tcp2_example_com, tcp3_example_com);
  EXPECT_EQ(tcp2_ibm_com, tcp3_ibm_com);

  // Now add and remove a host that we never have a conn pool to. This should not lead to any
  // drain callbacks, etc.
  dns_timer_->callback_();
  dns_callback(TestUtility::makeDnsResponse({"127.0.0.2", "127.0.0.3"}));

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
      dns_resolvers:
      - socket_address:
          address: 1.2.3.4
          port_value: 80
      lb_policy: ROUND_ROBIN
      hosts:
      - socket_address:
          address: localhost
          port_value: 11001
  )EOF";

  std::shared_ptr<Network::MockDnsResolver> dns_resolver(new Network::MockDnsResolver());
  EXPECT_CALL(factory_.dispatcher_, createDnsResolver(_)).WillOnce(Return(dns_resolver));

  Network::DnsResolver::ResolveCb dns_callback;
  Event::MockTimer* dns_timer_ = new NiceMock<Event::MockTimer>(&factory_.dispatcher_);
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV2Yaml(yaml));
  EXPECT_FALSE(cluster_manager_->get("cluster_1")->info()->addedViaApi());

  dns_callback(TestUtility::makeDnsResponse({"127.0.0.2"}));

  EXPECT_CALL(factory_, allocateConnPool_(_))
      .WillOnce(ReturnNew<Http::ConnectionPool::MockInstance>());

  EXPECT_CALL(factory_, allocateTcpConnPool_(_))
      .WillOnce(ReturnNew<Tcp::ConnectionPool::MockInstance>());

  Http::ConnectionPool::MockInstance* cp =
      dynamic_cast<Http::ConnectionPool::MockInstance*>(cluster_manager_->httpConnPoolForCluster(
          "cluster_1", ResourcePriority::Default, Http::Protocol::Http11, nullptr));

  Tcp::ConnectionPool::MockInstance* tcp =
      dynamic_cast<Tcp::ConnectionPool::MockInstance*>(cluster_manager_->tcpConnPoolForCluster(
          "cluster_1", ResourcePriority::Default, nullptr, nullptr));

  // Immediate drain, since this can happen with the HTTP codecs.
  EXPECT_CALL(*cp, addDrainedCallback(_))
      .WillOnce(Invoke([](Http::ConnectionPool::Instance::DrainedCb cb) { cb(); }));

  EXPECT_CALL(*tcp, addDrainedCallback(_))
      .WillOnce(Invoke([](Tcp::ConnectionPool::Instance::DrainedCb cb) { cb(); }));

  // Remove the first host, this should lead to the cp being drained, without
  // crash.
  dns_timer_->callback_();
  dns_callback(TestUtility::makeDnsResponse({}));

  factory_.tls_.shutdownThread();
}

class MockConnPoolWithDestroy : public Http::ConnectionPool::MockInstance {
public:
  ~MockConnPoolWithDestroy() { onDestroy(); }

  MOCK_METHOD0(onDestroy, void());
};

class MockTcpConnPoolWithDestroy : public Tcp::ConnectionPool::MockInstance {
public:
  ~MockTcpConnPoolWithDestroy() { onDestroy(); }

  MOCK_METHOD0(onDestroy, void());
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
      dns_resolvers:
      - socket_address:
          address: 1.2.3.4
          port_value: 80
      lb_policy: ROUND_ROBIN
      hosts:
      - socket_address:
          address: localhost
          port_value: 11001
  )EOF";

  std::shared_ptr<Network::MockDnsResolver> dns_resolver(new Network::MockDnsResolver());
  EXPECT_CALL(factory_.dispatcher_, createDnsResolver(_)).WillOnce(Return(dns_resolver));

  Network::DnsResolver::ResolveCb dns_callback;
  Event::MockTimer* dns_timer_ = new NiceMock<Event::MockTimer>(&factory_.dispatcher_);
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV2Yaml(yaml));
  EXPECT_FALSE(cluster_manager_->get("cluster_1")->info()->addedViaApi());

  dns_callback(TestUtility::makeDnsResponse({"127.0.0.2"}));

  MockConnPoolWithDestroy* mock_cp = new MockConnPoolWithDestroy();
  EXPECT_CALL(factory_, allocateConnPool_(_)).WillOnce(Return(mock_cp));

  MockTcpConnPoolWithDestroy* mock_tcp = new MockTcpConnPoolWithDestroy();
  EXPECT_CALL(factory_, allocateTcpConnPool_(_)).WillOnce(Return(mock_tcp));

  Http::ConnectionPool::MockInstance* cp =
      dynamic_cast<Http::ConnectionPool::MockInstance*>(cluster_manager_->httpConnPoolForCluster(
          "cluster_1", ResourcePriority::Default, Http::Protocol::Http11, nullptr));

  Tcp::ConnectionPool::MockInstance* tcp =
      dynamic_cast<Tcp::ConnectionPool::MockInstance*>(cluster_manager_->tcpConnPoolForCluster(
          "cluster_1", ResourcePriority::Default, nullptr, nullptr));

  // Remove the first host, this should lead to the cp being drained.
  Http::ConnectionPool::Instance::DrainedCb drained_cb;
  EXPECT_CALL(*cp, addDrainedCallback(_)).WillOnce(SaveArg<0>(&drained_cb));
  Tcp::ConnectionPool::Instance::DrainedCb tcp_drained_cb;
  EXPECT_CALL(*tcp, addDrainedCallback(_)).WillOnce(SaveArg<0>(&tcp_drained_cb));
  dns_timer_->callback_();
  dns_callback(TestUtility::makeDnsResponse({}));

  // The drained callback might get called when the CP is being destroyed.
  EXPECT_CALL(*mock_cp, onDestroy()).WillOnce(Invoke(drained_cb));
  EXPECT_CALL(*mock_tcp, onDestroy()).WillOnce(Invoke(tcp_drained_cb));
  factory_.tls_.shutdownThread();
}

TEST_F(ClusterManagerImplTest, OriginalDstInitialization) {
  const std::string json = R"EOF(
  {
    "clusters": [
    {
      "name": "cluster_1",
      "connect_timeout_ms": 250,
      "type": "original_dst",
      "lb_type": "original_dst_lb"
    }]
  }
  )EOF";

  ReadyWatcher initialized;
  EXPECT_CALL(initialized, ready());

  create(parseBootstrapFromJson(json));

  // Set up for an initialize callback.
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

  EXPECT_FALSE(cluster_manager_->get("cluster_1")->info()->addedViaApi());

  // Test for no hosts returning the correct values before we have hosts.
  EXPECT_EQ(nullptr, cluster_manager_->httpConnPoolForCluster(
                         "cluster_1", ResourcePriority::Default, Http::Protocol::Http11, nullptr));
  EXPECT_EQ(nullptr, cluster_manager_->tcpConnPoolForCluster("cluster_1", ResourcePriority::Default,
                                                             nullptr, nullptr));
  EXPECT_EQ(nullptr,
            cluster_manager_->tcpConnForCluster("cluster_1", nullptr, nullptr).connection_);
  EXPECT_EQ(3UL, factory_.stats_.counter("cluster.cluster_1.upstream_cx_none_healthy").value());

  factory_.tls_.shutdownThread();
}

// Tests that all the HC/weight/metadata changes are delivered in one go, as long as
// there's no hosts changes in between.
// Also tests that if hosts are added/removed between mergeable updates, delivery will
// happen and the scheduled update will be canceled.
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
        // Triggerd by the 2 HC updates, it's a merged update so no added/removed
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

  Event::MockTimer* timer = new NiceMock<Event::MockTimer>(&factory_.dispatcher_);
  const Cluster& cluster = cluster_manager_->clusters().begin()->second;
  HostVectorSharedPtr hosts(
      new HostVector(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()));
  HostsPerLocalitySharedPtr hosts_per_locality = std::make_shared<HostsPerLocalityImpl>();
  HostVector hosts_added;
  HostVector hosts_removed;

  // The first update should be applied immediately, since it's not mergeable.
  hosts_removed.push_back((*hosts)[0]);
  cluster.prioritySet().hostSetsPerPriority()[0]->updateHosts(hosts, hosts, hosts_per_locality,
                                                              hosts_per_locality, {}, hosts_added,
                                                              hosts_removed, absl::nullopt);
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());

  // These calls should be merged, since there are no added/removed hosts.
  hosts_removed.clear();
  cluster.prioritySet().hostSetsPerPriority()[0]->updateHosts(hosts, hosts, hosts_per_locality,
                                                              hosts_per_locality, {}, hosts_added,
                                                              hosts_removed, absl::nullopt);
  cluster.prioritySet().hostSetsPerPriority()[0]->updateHosts(hosts, hosts, hosts_per_locality,
                                                              hosts_per_locality, {}, hosts_added,
                                                              hosts_removed, absl::nullopt);
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());

  // Ensure the merged updates were applied.
  timer->callback_();
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());

  // Add the host back, the update should be immediately applied.
  hosts_removed.clear();
  hosts_added.push_back((*hosts)[0]);
  cluster.prioritySet().hostSetsPerPriority()[0]->updateHosts(hosts, hosts, hosts_per_locality,
                                                              hosts_per_locality, {}, hosts_added,
                                                              hosts_removed, absl::nullopt);
  EXPECT_EQ(2, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());

  // Now emit 3 updates that should be scheduled: metadata, HC, and weight.
  hosts_added.clear();

  (*hosts)[0]->metadata(buildMetadata("v1"));
  cluster.prioritySet().hostSetsPerPriority()[0]->updateHosts(hosts, hosts, hosts_per_locality,
                                                              hosts_per_locality, {}, hosts_added,
                                                              hosts_removed, absl::nullopt);

  (*hosts)[0]->healthFlagSet(Host::HealthFlag::FAILED_EDS_HEALTH);
  cluster.prioritySet().hostSetsPerPriority()[0]->updateHosts(hosts, hosts, hosts_per_locality,
                                                              hosts_per_locality, {}, hosts_added,
                                                              hosts_removed, absl::nullopt);

  (*hosts)[0]->weight(100);
  cluster.prioritySet().hostSetsPerPriority()[0]->updateHosts(hosts, hosts, hosts_per_locality,
                                                              hosts_per_locality, {}, hosts_added,
                                                              hosts_removed, absl::nullopt);

  // Updates not delivered yet.
  EXPECT_EQ(2, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());

  // Remove the host again, should cancel the scheduled update and be delivered immediately.
  hosts_removed.push_back((*hosts)[0]);
  cluster.prioritySet().hostSetsPerPriority()[0]->updateHosts(hosts, hosts, hosts_per_locality,
                                                              hosts_per_locality, {}, hosts_added,
                                                              hosts_removed, absl::nullopt);

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

  const Cluster& cluster = cluster_manager_->clusters().begin()->second;
  HostVectorSharedPtr hosts(
      new HostVector(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()));
  HostsPerLocalitySharedPtr hosts_per_locality = std::make_shared<HostsPerLocalityImpl>();
  HostVector hosts_added;
  HostVector hosts_removed;

  // The first update should be applied immediately, because even though it's mergeable
  // it's outside the default merge window of 3 seconds (found in debugger as value of
  // cluster.info()->lbConfig().update_merge_window() in ClusterManagerImpl::scheduleUpdate.
  time_system_.sleep(std::chrono::seconds(60));
  cluster.prioritySet().hostSetsPerPriority()[0]->updateHosts(hosts, hosts, hosts_per_locality,
                                                              hosts_per_locality, {}, hosts_added,
                                                              hosts_removed, absl::nullopt);
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.update_out_of_merge_window").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());
}

// Tests that mergeable updates inside of a window are not applied immediately.
TEST_F(ClusterManagerImplTest, MergedUpdatesInsideWindow) {
  createWithLocalClusterUpdate();

  const Cluster& cluster = cluster_manager_->clusters().begin()->second;
  HostVectorSharedPtr hosts(
      new HostVector(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()));
  HostsPerLocalitySharedPtr hosts_per_locality = std::make_shared<HostsPerLocalityImpl>();
  HostVector hosts_added;
  HostVector hosts_removed;

  // The first update will not be applied, as we make it inside the default mergeable window of
  // 3 seconds (found in debugger as value of cluster.info()->lbConfig().update_merge_window()
  // in ClusterManagerImpl::scheduleUpdate. Note that initially the update-time is
  // default-initialized to a monotonic time of 0, as is SimulatedTimeSystem::monotonic_time_.
  time_system_.sleep(std::chrono::seconds(2));
  cluster.prioritySet().hostSetsPerPriority()[0]->updateHosts(hosts, hosts, hosts_per_locality,
                                                              hosts_per_locality, {}, hosts_added,
                                                              hosts_removed, absl::nullopt);
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

  const Cluster& cluster = cluster_manager_->clusters().begin()->second;
  HostVectorSharedPtr hosts(
      new HostVector(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()));
  HostsPerLocalitySharedPtr hosts_per_locality = std::make_shared<HostsPerLocalityImpl>();
  HostVector hosts_added;
  HostVector hosts_removed;

  // The first update should be applied immediately, because even though it's mergeable
  // and outside a merge window, merging is disabled.
  cluster.prioritySet().hostSetsPerPriority()[0]->updateHosts(hosts, hosts, hosts_per_locality,
                                                              hosts_per_locality, {}, hosts_added,
                                                              hosts_removed, absl::nullopt);
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

  Event::MockTimer* timer = new NiceMock<Event::MockTimer>(&factory_.dispatcher_);

  // We can't used the bootstrap cluster, so add one dynamically.
  const std::string yaml = R"EOF(
  name: new_cluster
  connect_timeout: 0.250s
  type: STATIC
  lb_policy: ROUND_ROBIN
  hosts:
    - socket_address:
        address: 127.0.0.1
        port_value: 12001
  common_lb_config:
    update_merge_window: 3s
  )EOF";
  EXPECT_TRUE(cluster_manager_->addOrUpdateCluster(parseClusterFromV2Yaml(yaml), "version1"));

  const Cluster& cluster = cluster_manager_->clusters().find("new_cluster")->second;
  HostVectorSharedPtr hosts(
      new HostVector(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()));
  HostsPerLocalitySharedPtr hosts_per_locality = std::make_shared<HostsPerLocalityImpl>();
  HostVector hosts_added;
  HostVector hosts_removed;

  // The first update should be applied immediately, since it's not mergeable.
  hosts_removed.push_back((*hosts)[0]);
  cluster.prioritySet().hostSetsPerPriority()[0]->updateHosts(hosts, hosts, hosts_per_locality,
                                                              hosts_per_locality, {}, hosts_added,
                                                              hosts_removed, absl::nullopt);
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());

  // These calls should be merged, since there are no added/removed hosts.
  hosts_removed.clear();
  cluster.prioritySet().hostSetsPerPriority()[0]->updateHosts(hosts, hosts, hosts_per_locality,
                                                              hosts_per_locality, {}, hosts_added,
                                                              hosts_removed, absl::nullopt);
  cluster.prioritySet().hostSetsPerPriority()[0]->updateHosts(hosts, hosts, hosts_per_locality,
                                                              hosts_per_locality, {}, hosts_added,
                                                              hosts_removed, absl::nullopt);
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());

  // Update the cluster, which should cancel the pending updates.
  std::shared_ptr<MockCluster> updated(new NiceMock<MockCluster>());
  updated->info_->name_ = "new_cluster";
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _, true)).WillOnce(Return(updated));

  const std::string yaml_updated = R"EOF(
  name: new_cluster
  connect_timeout: 0.250s
  type: STATIC
  lb_policy: ROUND_ROBIN
  hosts:
    - socket_address:
        address: 127.0.0.1
        port_value: 12001
  common_lb_config:
    update_merge_window: 4s
  )EOF";

  // Add the updated cluster.
  EXPECT_EQ(2, factory_.stats_.gauge("cluster_manager.active_clusters").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_modified").value());
  EXPECT_EQ(0, factory_.stats_.gauge("cluster_manager.warming_clusters").value());
  EXPECT_TRUE(
      cluster_manager_->addOrUpdateCluster(parseClusterFromV2Yaml(yaml_updated), "version2"));
  EXPECT_EQ(2, factory_.stats_.gauge("cluster_manager.active_clusters").value());
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_modified").value());
  EXPECT_EQ(1, factory_.stats_.gauge("cluster_manager.warming_clusters").value());

  // Promote the updated cluster from warming to active & assert the old timer was disabled
  // and it won't be called on version1 of new_cluster.
  EXPECT_CALL(*timer, disableTimer());
  updated->initialize_callback_();

  EXPECT_EQ(2, factory_.stats_.gauge("cluster_manager.active_clusters").value());
  EXPECT_EQ(0, factory_.stats_.gauge("cluster_manager.warming_clusters").value());
}

class ClusterManagerInitHelperTest : public testing::Test {
public:
  MOCK_METHOD1(onClusterInit, void(Cluster& cluster));

  ClusterManagerInitHelper init_helper_{[this](Cluster& cluster) { onClusterInit(cluster); }};
};

TEST_F(ClusterManagerInitHelperTest, ImmediateInitialize) {
  InSequence s;

  NiceMock<MockCluster> cluster1;
  ON_CALL(cluster1, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(cluster1, initialize(_));
  init_helper_.addCluster(cluster1);
  EXPECT_CALL(*this, onClusterInit(Ref(cluster1)));
  cluster1.initialize_callback_();

  init_helper_.onStaticLoadComplete();

  ReadyWatcher cm_initialized;
  EXPECT_CALL(cm_initialized, ready());
  init_helper_.setInitializedCb([&]() -> void { cm_initialized.ready(); });
}

TEST_F(ClusterManagerInitHelperTest, StaticSdsInitialize) {
  InSequence s;

  NiceMock<MockCluster> sds;
  ON_CALL(sds, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(sds, initialize(_));
  init_helper_.addCluster(sds);
  EXPECT_CALL(*this, onClusterInit(Ref(sds)));
  sds.initialize_callback_();

  NiceMock<MockCluster> cluster1;
  ON_CALL(cluster1, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Secondary));
  init_helper_.addCluster(cluster1);

  EXPECT_CALL(cluster1, initialize(_));
  init_helper_.onStaticLoadComplete();

  ReadyWatcher cm_initialized;
  init_helper_.setInitializedCb([&]() -> void { cm_initialized.ready(); });

  EXPECT_CALL(*this, onClusterInit(Ref(cluster1)));
  EXPECT_CALL(cm_initialized, ready());
  cluster1.initialize_callback_();
}

TEST_F(ClusterManagerInitHelperTest, UpdateAlreadyInitialized) {
  InSequence s;

  ReadyWatcher cm_initialized;
  init_helper_.setInitializedCb([&]() -> void { cm_initialized.ready(); });

  NiceMock<MockCluster> cluster1;
  ON_CALL(cluster1, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(cluster1, initialize(_));
  init_helper_.addCluster(cluster1);

  NiceMock<MockCluster> cluster2;
  ON_CALL(cluster2, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(cluster2, initialize(_));
  init_helper_.addCluster(cluster2);

  init_helper_.onStaticLoadComplete();

  EXPECT_CALL(*this, onClusterInit(Ref(cluster1)));
  cluster1.initialize_callback_();
  init_helper_.removeCluster(cluster1);

  EXPECT_CALL(*this, onClusterInit(Ref(cluster2)));
  EXPECT_CALL(cm_initialized, ready());
  cluster2.initialize_callback_();
}

TEST_F(ClusterManagerInitHelperTest, AddSecondaryAfterSecondaryInit) {
  InSequence s;

  ReadyWatcher cm_initialized;
  init_helper_.setInitializedCb([&]() -> void { cm_initialized.ready(); });

  NiceMock<MockCluster> cluster1;
  ON_CALL(cluster1, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(cluster1, initialize(_));
  init_helper_.addCluster(cluster1);

  NiceMock<MockCluster> cluster2;
  ON_CALL(cluster2, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Secondary));
  init_helper_.addCluster(cluster2);

  init_helper_.onStaticLoadComplete();

  EXPECT_CALL(*this, onClusterInit(Ref(cluster1)));
  EXPECT_CALL(cluster2, initialize(_));
  cluster1.initialize_callback_();

  NiceMock<MockCluster> cluster3;
  ON_CALL(cluster3, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Secondary));
  EXPECT_CALL(cluster3, initialize(_));
  init_helper_.addCluster(cluster3);

  EXPECT_CALL(*this, onClusterInit(Ref(cluster3)));
  cluster3.initialize_callback_();
  EXPECT_CALL(*this, onClusterInit(Ref(cluster2)));
  EXPECT_CALL(cm_initialized, ready());
  cluster2.initialize_callback_();
}

// Tests the scenario encountered in Issue 903: The cluster was removed from
// the secondary init list while traversing the list.
TEST_F(ClusterManagerInitHelperTest, RemoveClusterWithinInitLoop) {
  InSequence s;
  NiceMock<MockCluster> cluster;
  ON_CALL(cluster, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Secondary));
  init_helper_.addCluster(cluster);

  // Set up the scenario seen in Issue 903 where initialize() ultimately results
  // in the removeCluster() call. In the real bug this was a long and complex call
  // chain.
  EXPECT_CALL(cluster, initialize(_)).WillOnce(Invoke([&](std::function<void()>) -> void {
    init_helper_.removeCluster(cluster);
  }));

  // Now call onStaticLoadComplete which will exercise maybeFinishInitialize()
  // which calls initialize() on the members of the secondary init list.
  init_helper_.onStaticLoadComplete();
}

// Validate that when options are set in the ClusterManager and/or Cluster, we see the socket option
// propagated to setsockopt(). This is as close to an end-to-end test as we have for this feature,
// due to the complexity of creating an integration test involving the network stack. We only test
// the IPv4 case here, as the logic around IPv4/IPv6 handling is tested generically in
// socket_option_impl_test.cc.
class SockoptsTest : public ClusterManagerImplTest {
public:
  void initialize(const std::string& yaml) { create(parseBootstrapFromV2Yaml(yaml)); }

  void TearDown() override { factory_.tls_.shutdownThread(); }

  // TODO(tschroed): Extend this to support socket state as well.
  void expectSetsockopts(const std::vector<std::pair<Network::SocketOptionName, int>>& names_vals) {

    NiceMock<Api::MockOsSysCalls> os_sys_calls;
    TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
    bool expect_success = true;
    for (const auto& name_val : names_vals) {
      if (!name_val.first.has_value()) {
        expect_success = false;
        continue;
      }
      EXPECT_CALL(os_sys_calls, setsockopt_(_, name_val.first.value().first,
                                            name_val.first.value().second, _, sizeof(int)))
          .WillOnce(Invoke([&name_val](int, int, int, const void* optval, socklen_t) -> int {
            EXPECT_EQ(name_val.second, *static_cast<const int*>(optval));
            return 0;
          }));
    }
    EXPECT_CALL(factory_.tls_.dispatcher_, createClientConnection_(_, _, _, _))
        .WillOnce(Invoke([this, &names_vals, expect_success](
                             Network::Address::InstanceConstSharedPtr,
                             Network::Address::InstanceConstSharedPtr, Network::TransportSocketPtr&,
                             const Network::ConnectionSocket::OptionsSharedPtr& options)
                             -> Network::ClientConnection* {
          EXPECT_NE(nullptr, options.get()) << "Unexpected null options";
          if (options.get() != nullptr) { // Don't crash the entire test.
            EXPECT_EQ(names_vals.size(), options->size());
          }
          NiceMock<Network::MockConnectionSocket> socket;
          if (expect_success) {
            EXPECT_TRUE((Network::Socket::applyOptions(
                options, socket, envoy::api::v2::core::SocketOption::STATE_PREBIND)));
          } else {
            EXPECT_FALSE((Network::Socket::applyOptions(
                options, socket, envoy::api::v2::core::SocketOption::STATE_PREBIND)));
          }
          return connection_;
        }));
    cluster_manager_->tcpConnForCluster("SockoptsCluster", nullptr, nullptr);
  }

  void expectSetsockoptFreebind() {
    std::vector<std::pair<Network::SocketOptionName, int>> names_vals{
        {ENVOY_SOCKET_IP_FREEBIND, 1}};
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
    auto conn_data = cluster_manager_->tcpConnForCluster("SockoptsCluster", nullptr, nullptr);
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
      hosts:
      - socket_address:
          address: "127.0.0.1"
          port_value: 11001
  )EOF";
  initialize(yaml);
  expectNoSocketOptions();
}

TEST_F(SockoptsTest, FreebindClusterOnly) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: SockoptsCluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      hosts:
      - socket_address:
          address: "127.0.0.1"
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
      hosts:
      - socket_address:
          address: "127.0.0.1"
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
      hosts:
      - socket_address:
          address: "127.0.0.1"
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
      hosts:
      - socket_address:
          address: "127.0.0.1"
          port_value: 11001
      upstream_bind_config:
        socket_options: [
          { level: 1, name: 2, int_value: 3, state: STATE_PREBIND },
          { level: 4, name: 5, int_value: 6, state: STATE_PREBIND }]

  )EOF";
  initialize(yaml);
  std::vector<std::pair<Network::SocketOptionName, int>> names_vals{
      {Network::SocketOptionName({1, 2}), 3}, {Network::SocketOptionName({4, 5}), 6}};
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
      hosts:
      - socket_address:
          address: "127.0.0.1"
          port_value: 11001
  cluster_manager:
    upstream_bind_config:
      socket_options: [
        { level: 1, name: 2, int_value: 3, state: STATE_PREBIND },
        { level: 4, name: 5, int_value: 6, state: STATE_PREBIND }]
  )EOF";
  initialize(yaml);
  std::vector<std::pair<Network::SocketOptionName, int>> names_vals{
      {Network::SocketOptionName({1, 2}), 3}, {Network::SocketOptionName({4, 5}), 6}};
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
      hosts:
      - socket_address:
          address: "127.0.0.1"
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
  std::vector<std::pair<Network::SocketOptionName, int>> names_vals{
      {Network::SocketOptionName({1, 2}), 3}, {Network::SocketOptionName({4, 5}), 6}};
  expectSetsockopts(names_vals);
}

// Validate that when tcp keepalives are set in the Cluster, we see the socket
// option propagated to setsockopt(). This is as close to an end-to-end test as we have for this
// feature, due to the complexity of creating an integration test involving the network stack. We
// only test the IPv4 case here, as the logic around IPv4/IPv6 handling is tested generically in
// tcp_keepalive_option_impl_test.cc.
class TcpKeepaliveTest : public ClusterManagerImplTest {
public:
  void initialize(const std::string& yaml) { create(parseBootstrapFromV2Yaml(yaml)); }

  void TearDown() override { factory_.tls_.shutdownThread(); }

  void expectSetsockoptSoKeepalive(absl::optional<int> keepalive_probes,
                                   absl::optional<int> keepalive_time,
                                   absl::optional<int> keepalive_interval) {
    if (!ENVOY_SOCKET_SO_KEEPALIVE.has_value()) {
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
                    options, socket, envoy::api::v2::core::SocketOption::STATE_PREBIND)));
                return connection_;
              }));
      cluster_manager_->tcpConnForCluster("TcpKeepaliveCluster", nullptr, nullptr);
      return;
    }
    NiceMock<Api::MockOsSysCalls> os_sys_calls;
    TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
    EXPECT_CALL(factory_.tls_.dispatcher_, createClientConnection_(_, _, _, _))
        .WillOnce(
            Invoke([this](Network::Address::InstanceConstSharedPtr,
                          Network::Address::InstanceConstSharedPtr, Network::TransportSocketPtr&,
                          const Network::ConnectionSocket::OptionsSharedPtr& options)
                       -> Network::ClientConnection* {
              EXPECT_NE(nullptr, options.get());
              NiceMock<Network::MockConnectionSocket> socket;
              EXPECT_TRUE((Network::Socket::applyOptions(
                  options, socket, envoy::api::v2::core::SocketOption::STATE_PREBIND)));
              return connection_;
            }));
    EXPECT_CALL(os_sys_calls, setsockopt_(_, ENVOY_SOCKET_SO_KEEPALIVE.value().first,
                                          ENVOY_SOCKET_SO_KEEPALIVE.value().second, _, sizeof(int)))
        .WillOnce(Invoke([](int, int, int, const void* optval, socklen_t) -> int {
          EXPECT_EQ(1, *static_cast<const int*>(optval));
          return 0;
        }));
    if (keepalive_probes.has_value()) {
      EXPECT_CALL(os_sys_calls,
                  setsockopt_(_, ENVOY_SOCKET_TCP_KEEPCNT.value().first,
                              ENVOY_SOCKET_TCP_KEEPCNT.value().second, _, sizeof(int)))
          .WillOnce(
              Invoke([&keepalive_probes](int, int, int, const void* optval, socklen_t) -> int {
                EXPECT_EQ(keepalive_probes.value(), *static_cast<const int*>(optval));
                return 0;
              }));
    }
    if (keepalive_time.has_value()) {
      EXPECT_CALL(os_sys_calls,
                  setsockopt_(_, ENVOY_SOCKET_TCP_KEEPIDLE.value().first,
                              ENVOY_SOCKET_TCP_KEEPIDLE.value().second, _, sizeof(int)))
          .WillOnce(Invoke([&keepalive_time](int, int, int, const void* optval, socklen_t) -> int {
            EXPECT_EQ(keepalive_time.value(), *static_cast<const int*>(optval));
            return 0;
          }));
    }
    if (keepalive_interval.has_value()) {
      EXPECT_CALL(os_sys_calls,
                  setsockopt_(_, ENVOY_SOCKET_TCP_KEEPINTVL.value().first,
                              ENVOY_SOCKET_TCP_KEEPINTVL.value().second, _, sizeof(int)))
          .WillOnce(
              Invoke([&keepalive_interval](int, int, int, const void* optval, socklen_t) -> int {
                EXPECT_EQ(keepalive_interval.value(), *static_cast<const int*>(optval));
                return 0;
              }));
    }
    auto conn_data = cluster_manager_->tcpConnForCluster("TcpKeepaliveCluster", nullptr, nullptr);
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
    auto conn_data = cluster_manager_->tcpConnForCluster("TcpKeepaliveCluster", nullptr, nullptr);
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
      hosts:
      - socket_address:
          address: "127.0.0.1"
          port_value: 11001
  )EOF";
  initialize(yaml);
  expectNoSocketOptions();
}

TEST_F(TcpKeepaliveTest, TcpKeepaliveCluster) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: TcpKeepaliveCluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      hosts:
      - socket_address:
          address: "127.0.0.1"
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
      hosts:
      - socket_address:
          address: "127.0.0.1"
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
      hosts:
      - socket_address:
          address: "127.0.0.1"
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

} // namespace
} // namespace Upstream
} // namespace Envoy
