#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.validate.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/extensions/clusters/eds/eds.h"
#include "source/extensions/clusters/static/static_cluster.h"

#include "test/common/upstream/test_cluster_manager.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Upstream {
namespace {

using testing::_;

using ClusterType = absl::variant<envoy::config::cluster::v3::Cluster::DiscoveryType,
                                  envoy::config::cluster::v3::Cluster::CustomClusterType>;

void setClusterType(envoy::config::cluster::v3::Cluster& cluster, const ClusterType& cluster_type) {
  cluster.clear_cluster_discovery_type();
  if (absl::holds_alternative<envoy::config::cluster::v3::Cluster::DiscoveryType>(cluster_type)) {
    cluster.set_type(absl::get<envoy::config::cluster::v3::Cluster::DiscoveryType>(cluster_type));
  } else if (absl::holds_alternative<envoy::config::cluster::v3::Cluster::CustomClusterType>(
                 cluster_type)) {
    cluster.mutable_cluster_type()->CopyFrom(
        absl::get<envoy::config::cluster::v3::Cluster::CustomClusterType>(cluster_type));
  }
}

bool hostsInHostsVector(const Envoy::Upstream::HostVector& host_vector,
                        std::vector<uint32_t> host_ports) {
  size_t matches = 0;
  std::sort(host_ports.begin(), host_ports.end());
  for (const auto& host : host_vector) {
    if (std::binary_search(host_ports.begin(), host_ports.end(), host->address()->ip()->port())) {
      ++matches;
    }
  }
  return matches == host_ports.size();
}

envoy::config::cluster::v3::Cluster parseClusterFromV3Yaml(const std::string& yaml_config,
                                                           const ClusterType& cluster_type) {
  auto cluster = parseClusterFromV3Yaml(yaml_config);
  setClusterType(cluster, cluster_type);
  return cluster;
}

class DeferredClusterInitializationTest : public testing::TestWithParam<bool> {
protected:
  DeferredClusterInitializationTest()
      : http_context_(factory_.stats_.symbolTable()), grpc_context_(factory_.stats_.symbolTable()),
        router_context_(factory_.stats_.symbolTable()) {}

  void create(const envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    cluster_manager_ = TestClusterManagerImpl::createAndInit(
        bootstrap, factory_, factory_.server_context_, factory_.stats_, factory_.tls_,
        factory_.runtime_, factory_.local_info_, log_manager_, factory_.dispatcher_, admin_,
        validation_context_, *factory_.api_, http_context_, grpc_context_, router_context_,
        server_);
    cluster_manager_->setPrimaryClustersInitializedCb([this, bootstrap]() {
      THROW_IF_NOT_OK(cluster_manager_->initializeSecondaryClusters(bootstrap));
    });
  }

  ClusterType getStaticClusterType() const {
    if (GetParam()) {
      envoy::config::cluster::v3::Cluster::CustomClusterType custom_cluster_type;
      custom_cluster_type.set_name("envoy.cluster.static");
      return custom_cluster_type;
    }

    return envoy::config::cluster::v3::Cluster::STATIC;
  }

  ClusterType getEdsClusterType() const {
    if (GetParam()) {
      ASSERT(false, "EDS cluster support via CustomClusterType unimplemented.");
      envoy::config::cluster::v3::Cluster::CustomClusterType custom_cluster_type;
      return custom_cluster_type;
    }

    return envoy::config::cluster::v3::Cluster::EDS;
  }

  envoy::config::bootstrap::v3::Bootstrap
  parseBootstrapFromV3YamlEnableDeferredCluster(const std::string& yaml) {
    envoy::config::bootstrap::v3::Bootstrap bootstrap;
    TestUtility::loadFromYaml(yaml, bootstrap);
    bootstrap.mutable_cluster_manager()->set_enable_deferred_cluster_creation(true);
    ClusterType cluster_type = getStaticClusterType();
    for (auto& cluster : *bootstrap.mutable_static_resources()->mutable_clusters()) {
      setClusterType(cluster, cluster_type);
    }
    return bootstrap;
  }

  uint64_t readGauge(const std::string& gauge_name) const {
    auto gauge_or = factory_.stats_.findGaugeByString(gauge_name);
    ASSERT(gauge_or.has_value());
    return gauge_or.value().get().value();
  }

  NiceMock<TestClusterManagerFactory> factory_;
  NiceMock<ProtobufMessage::MockValidationContext> validation_context_;
  std::unique_ptr<TestClusterManagerImpl> cluster_manager_;
  AccessLog::MockAccessLogManager log_manager_;
  NiceMock<Server::MockAdmin> admin_;
  Http::ContextImpl http_context_;
  Grpc::ContextImpl grpc_context_;
  Router::ContextImpl router_context_;
  NiceMock<Server::MockInstance> server_;
};

class StaticClusterTest : public DeferredClusterInitializationTest {};

INSTANTIATE_TEST_SUITE_P(UseCustomClusterType, StaticClusterTest, testing::Bool());

// Test that bootstrap static clusters are deferred initialized.
TEST_P(StaticClusterTest, BootstrapStaticClustersAreDeferredInitialized) {
  const std::string yaml = R"EOF(
    static_resources:
      clusters:
      - name: cluster_1
        connect_timeout: 0.250s
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

  auto bootstrap = parseBootstrapFromV3YamlEnableDeferredCluster(yaml);

  EXPECT_LOG_CONTAINS("debug", "Deferring add or update for TLS cluster cluster_1",
                      create(bootstrap));
  EXPECT_EQ(readGauge("thread_local_cluster_manager.test_thread.clusters_inflated"), 0);
  EXPECT_LOG_CONTAINS("debug", "initializing TLS cluster cluster_1 inline",
                      cluster_manager_->getThreadLocalCluster("cluster_1"));
  EXPECT_EQ(readGauge("thread_local_cluster_manager.test_thread.clusters_inflated"), 1);
}

// Test that cds cluster are deferred initialized.
TEST_P(StaticClusterTest, CdsStaticClustersAreDeferredInitialized) {
  const std::string bootstrap_yaml = R"EOF(
    static_resources:
      clusters:
      - name: bootstrap_cluster
        connect_timeout: 0.250s
        lb_policy: ROUND_ROBIN
        load_assignment:
          cluster_name: bootstrap_cluster
          endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 60000
    )EOF";

  auto bootstrap = parseBootstrapFromV3YamlEnableDeferredCluster(bootstrap_yaml);
  create(bootstrap);

  EXPECT_EQ(readGauge("thread_local_cluster_manager.test_thread.clusters_inflated"), 0);
  const std::string static_cds_cluster_yaml = R"EOF(
    name: cluster_1
    connect_timeout: 0.250s
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

  EXPECT_LOG_CONTAINS("debug", "Deferring add or update for TLS cluster cluster_1", {
    EXPECT_TRUE(cluster_manager_->addOrUpdateCluster(
        parseClusterFromV3Yaml(static_cds_cluster_yaml, getStaticClusterType()), "version1"));
  });
  EXPECT_EQ(readGauge("thread_local_cluster_manager.test_thread.clusters_inflated"), 0);
  EXPECT_LOG_CONTAINS("debug", "initializing TLS cluster cluster_1 inline",
                      cluster_manager_->getThreadLocalCluster("cluster_1"));
  EXPECT_EQ(readGauge("thread_local_cluster_manager.test_thread.clusters_inflated"), 1);
}

// Test that we can merge deferred cds cluster configuration.
TEST_P(StaticClusterTest, MergeStaticCdsClusterUpdates) {
  const std::string bootstrap_yaml = R"EOF(
    static_resources:
    )EOF";

  auto bootstrap = parseBootstrapFromV3YamlEnableDeferredCluster(bootstrap_yaml);
  create(bootstrap);

  {
    const std::string static_cds_cluster_yaml_v1 = R"EOF(
    name: cluster_1
    connect_timeout: 0.250s
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: cluster_1
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 60000
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 60001
  )EOF";

    EXPECT_LOG_CONTAINS("debug", "Deferring add or update for TLS cluster cluster_1", {
      EXPECT_TRUE(cluster_manager_->addOrUpdateCluster(
          parseClusterFromV3Yaml(static_cds_cluster_yaml_v1, getStaticClusterType()), "version1"));
    });
    EXPECT_EQ(readGauge("thread_local_cluster_manager.test_thread.clusters_inflated"), 0);
  }

  {
    const std::string static_cds_cluster_yaml_v2 = R"EOF(
    name: cluster_1
    connect_timeout: 0.250s
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
        priority: 0
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 11002
        priority: 1
  )EOF";

    EXPECT_LOG_CONTAINS("debug", "Deferring add or update for TLS cluster cluster_1", {
      EXPECT_TRUE(cluster_manager_->addOrUpdateCluster(
          parseClusterFromV3Yaml(static_cds_cluster_yaml_v2, getStaticClusterType()), "version2"));
    });
    EXPECT_EQ(readGauge("thread_local_cluster_manager.test_thread.clusters_inflated"), 0);
  }

  EXPECT_LOG_CONTAINS("debug", "initializing TLS cluster cluster_1 inline",
                      cluster_manager_->getThreadLocalCluster("cluster_1"));
  EXPECT_EQ(readGauge("thread_local_cluster_manager.test_thread.clusters_inflated"), 1);
  Cluster& cluster = cluster_manager_->activeClusters().find("cluster_1")->second;

  // Check we only know of the two endpoints from the recent update.
  EXPECT_EQ(cluster.info()->endpointStats().membership_total_.value(), 2);
  EXPECT_EQ(cluster.prioritySet().crossPriorityHostMap()->size(), 2);
  auto& host_sets_vector = cluster.prioritySet().hostSetsPerPriority();
  for (auto& host_set : host_sets_vector) {
    EXPECT_EQ(host_set->hosts().size(), 1);
  }
}

// Test that an active deferred cds cluster can get updated after initialization.
TEST_P(StaticClusterTest, ActiveClusterGetsUpdated) {
  const std::string bootstrap_yaml = R"EOF(
    static_resources:
    )EOF";

  auto bootstrap = parseBootstrapFromV3YamlEnableDeferredCluster(bootstrap_yaml);
  create(bootstrap);

  {
    const std::string static_cds_cluster_yaml_v1 = R"EOF(
    name: cluster_1
    connect_timeout: 0.250s
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: cluster_1
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 60000
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 60001
  )EOF";

    EXPECT_LOG_CONTAINS("debug", "Deferring add or update for TLS cluster cluster_1", {
      EXPECT_TRUE(cluster_manager_->addOrUpdateCluster(
          parseClusterFromV3Yaml(static_cds_cluster_yaml_v1, getStaticClusterType()), "version1"));
    });
    EXPECT_LOG_CONTAINS("debug", "initializing TLS cluster cluster_1 inline",
                        cluster_manager_->getThreadLocalCluster("cluster_1"));
    EXPECT_EQ(readGauge("thread_local_cluster_manager.test_thread.clusters_inflated"), 1);
  }

  {
    const std::string static_cds_cluster_yaml_v2 = R"EOF(
    name: cluster_1
    connect_timeout: 0.250s
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
        priority: 0
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 11002
        priority: 1
  )EOF";
    // Expect this line to fail as we should just inflate as usual.
    EXPECT_LOG_CONTAINS("debug", "updating TLS cluster cluster_1", {
      EXPECT_TRUE(cluster_manager_->addOrUpdateCluster(
          parseClusterFromV3Yaml(static_cds_cluster_yaml_v2, getStaticClusterType()), "version2"));
    });
    EXPECT_EQ(readGauge("thread_local_cluster_manager.test_thread.clusters_inflated"), 1);
  }

  Cluster& cluster = cluster_manager_->activeClusters().find("cluster_1")->second;

  // Check we only know of the two endpoints from the recent update.
  EXPECT_EQ(cluster.info()->endpointStats().membership_total_.value(), 2);
  EXPECT_EQ(cluster.prioritySet().crossPriorityHostMap()->size(), 2);
  auto& host_sets_vector = cluster.prioritySet().hostSetsPerPriority();
  for (auto& host_set : host_sets_vector) {
    EXPECT_EQ(host_set->hosts().size(), 1);
  }
}

// Test that removed deferred cds clusters have their cluster initialization object removed.
TEST_P(StaticClusterTest, RemoveDeferredCluster) {
  const std::string bootstrap_yaml = R"EOF(
    static_resources:
    )EOF";

  auto bootstrap = parseBootstrapFromV3YamlEnableDeferredCluster(bootstrap_yaml);
  create(bootstrap);

  const std::string static_cds_cluster_yaml_v1 = R"EOF(
    name: cluster_1
    connect_timeout: 0.250s
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: cluster_1
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 60000
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 60001
  )EOF";

  EXPECT_LOG_CONTAINS("debug", "Deferring add or update for TLS cluster cluster_1", {
    EXPECT_TRUE(cluster_manager_->addOrUpdateCluster(
        parseClusterFromV3Yaml(static_cds_cluster_yaml_v1, getStaticClusterType()), "version1"));
  });
  EXPECT_EQ(readGauge("thread_local_cluster_manager.test_thread.clusters_inflated"), 0);

  cluster_manager_->removeCluster("cluster_1");
  EXPECT_EQ(factory_.stats_.counter("cluster_manager.cluster_removed").value(), 1);
  EXPECT_EQ(cluster_manager_->getThreadLocalCluster("cluster_1"), nullptr);
  EXPECT_EQ(readGauge("thread_local_cluster_manager.test_thread.clusters_inflated"), 0);
}

class MockConfigSubscriptionFactory : public Config::ConfigSubscriptionFactory {
public:
  std::string name() const override { return "envoy.config_subscription.rest"; }
  MOCK_METHOD(Config::SubscriptionPtr, create, (SubscriptionData & data), (override));
};

class EdsTest : public DeferredClusterInitializationTest {
protected:
  void doOnConfigUpdateVerifyNoThrow(
      const envoy::config::endpoint::v3::ClusterLoadAssignment& cluster_load_assignment) {
    const auto decoded_resources =
        TestUtility::decodeResources({cluster_load_assignment}, "cluster_name");
    EXPECT_TRUE(callbacks_->onConfigUpdate(decoded_resources.refvec_, {}, "").ok());
  }

  void addEndpoint(envoy::config::endpoint::v3::ClusterLoadAssignment& cluster_load_assignment,
                   uint32_t port, uint32_t priority = 0) {
    auto* endpoints = cluster_load_assignment.add_endpoints();
    endpoints->set_priority(priority);
    auto* socket_address = endpoints->add_lb_endpoints()
                               ->mutable_endpoint()
                               ->mutable_address()
                               ->mutable_socket_address();
    socket_address->set_address("1.2.3.4");
    socket_address->set_port_value(port);
  }

  NiceMock<MockConfigSubscriptionFactory> factory_;
  Registry::InjectFactory<Config::ConfigSubscriptionFactory> registered_{factory_};
  Config::SubscriptionCallbacks* callbacks_{nullptr};
};

// TODO(kbaichoo): when Eds Cluster supports getting its config via
// custom_cluster_type then we can enable these tests to run with that config as
// well.
INSTANTIATE_TEST_SUITE_P(UseCustomClusterType, EdsTest, testing::ValuesIn({false}));

// Test that hosts can be added to deferred initialized eds cluster.
TEST_P(EdsTest, ShouldMergeAddingHosts) {
  const std::string bootstrap_yaml = R"EOF(
    static_resources:
      clusters:
      - name: eds
        connect_timeout: 0.250s
        lb_policy: ROUND_ROBIN
        load_assignment:
          cluster_name: bootstrap_cluster
          endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 60000
    )EOF";

  auto bootstrap = parseBootstrapFromV3YamlEnableDeferredCluster(bootstrap_yaml);
  create(bootstrap);

  const std::string eds_cluster_yaml = R"EOF(
      name: cluster_1
      connect_timeout: 0.25s
      lb_policy: ROUND_ROBIN
      eds_cluster_config:
        service_name: fare
        eds_config:
          api_config_source:
            api_type: REST
            cluster_names:
            - eds
            refresh_delay: 1s
    )EOF";

  EXPECT_CALL(factory_, create(_))
      .WillOnce(testing::Invoke([this](Config::ConfigSubscriptionFactory::SubscriptionData& data) {
        callbacks_ = &data.callbacks_;
        return std::make_unique<NiceMock<Envoy::Config::MockSubscription>>();
      }));

  EXPECT_EQ(readGauge("thread_local_cluster_manager.test_thread.clusters_inflated"), 0);
  EXPECT_TRUE(cluster_manager_->addOrUpdateCluster(
      parseClusterFromV3Yaml(eds_cluster_yaml, getEdsClusterType()), "version1"));

  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  for (int i = 1; i < 11; ++i) {
    addEndpoint(cluster_load_assignment, 1000 * i);
    const auto decoded_resources =
        TestUtility::decodeResources({cluster_load_assignment}, "cluster_name");
    doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  }

  EXPECT_LOG_CONTAINS("debug", "initializing TLS cluster cluster_1 inline",
                      cluster_manager_->getThreadLocalCluster("cluster_1"));
  EXPECT_EQ(readGauge("thread_local_cluster_manager.test_thread.clusters_inflated"), 1);
  Cluster& cluster = cluster_manager_->activeClusters().find("cluster_1")->second;
  EXPECT_EQ(cluster.info()->endpointStats().membership_total_.value(), 10);
  EXPECT_TRUE(hostsInHostsVector(cluster.prioritySet().hostSetsPerPriority()[0]->hosts(),
                                 {1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000}));
}

TEST_P(EdsTest, ShouldNotMergeAddingHostsForDifferentClustersWithSameName) {
  const std::string bootstrap_yaml = R"EOF(
    static_resources:
      clusters:
      - name: eds
        connect_timeout: 0.250s
        lb_policy: ROUND_ROBIN
        load_assignment:
          cluster_name: bootstrap_cluster
          endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 60000
    )EOF";

  auto bootstrap = parseBootstrapFromV3YamlEnableDeferredCluster(bootstrap_yaml);
  create(bootstrap);

  EXPECT_CALL(factory_, create(_))
      .Times(2)
      .WillRepeatedly(
          testing::Invoke([this](Config::ConfigSubscriptionFactory::SubscriptionData& data) {
            callbacks_ = &data.callbacks_;
            return std::make_unique<NiceMock<Envoy::Config::MockSubscription>>();
          }));

  const std::string eds_cluster_yaml = R"EOF(
      name: cluster_1
      connect_timeout: 0.25s
      lb_policy: RING_HASH
      eds_cluster_config:
        service_name: fare
        eds_config:
          api_config_source:
            api_type: REST
            cluster_names:
            - eds
            refresh_delay: 1s
    )EOF";
  auto cluster = parseClusterFromV3Yaml(eds_cluster_yaml, getEdsClusterType());
  EXPECT_TRUE(cluster_manager_->addOrUpdateCluster(cluster, "version1"));

  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  addEndpoint(cluster_load_assignment, 1000);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  auto initiailization_instance =
      cluster_manager_->clusterInitializationMap().find("cluster_1")->second;
  EXPECT_NE(nullptr, initiailization_instance->load_balancer_factory_);
  // RING_HASH lb policy requires Envoy re-create the load balancer when the cluster is updated.
  EXPECT_TRUE(initiailization_instance->load_balancer_factory_->recreateOnHostChange());

  // Update the cluster with a different lb policy. Now it's a different cluster and should
  // not be merged.
  cluster.set_lb_policy(::envoy::config::cluster::v3::Cluster::ROUND_ROBIN);
  EXPECT_TRUE(cluster_manager_->addOrUpdateCluster(cluster, "version2"));

  // Because the eds_service_name is the same, we can reuse the same load assignment here.
  cluster_load_assignment.clear_endpoints();
  // Note we only add one endpoint to the priority 1 to the new cluster.
  addEndpoint(cluster_load_assignment, 100, 1);
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  auto new_initialization_instance =
      cluster_manager_->clusterInitializationMap().find("cluster_1")->second;
  EXPECT_NE(initiailization_instance.get(), new_initialization_instance.get());

  EXPECT_EQ(1, new_initialization_instance->per_priority_state_.at(1).hosts_added_.size());
  // Ensure the hosts_added_ is empty for priority 0. Because if unexpected merge happens,
  // the hosts_added_ will be non-empty.
  EXPECT_TRUE(!new_initialization_instance->per_priority_state_.contains(0) ||
              new_initialization_instance->per_priority_state_.at(0).hosts_added_.empty());
}

// Test that removed hosts do not appear when initializing a deferred eds cluster.
TEST_P(EdsTest, ShouldNotHaveRemovedHosts) {
  const std::string bootstrap_yaml = R"EOF(
    static_resources:
      clusters:
      - name: eds
        connect_timeout: 0.250s
        lb_policy: ROUND_ROBIN
        load_assignment:
          cluster_name: bootstrap_cluster
          endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 60000
    )EOF";

  auto bootstrap = parseBootstrapFromV3YamlEnableDeferredCluster(bootstrap_yaml);
  create(bootstrap);
  EXPECT_EQ(readGauge("thread_local_cluster_manager.test_thread.clusters_inflated"), 0);

  const std::string eds_cluster_yaml = R"EOF(
      name: cluster_1
      connect_timeout: 0.25s
      lb_policy: ROUND_ROBIN
      eds_cluster_config:
        service_name: fare
        eds_config:
          api_config_source:
            api_type: REST
            cluster_names:
            - eds
            refresh_delay: 1s
    )EOF";

  EXPECT_CALL(factory_, create(_))
      .WillOnce(testing::Invoke([this](Config::ConfigSubscriptionFactory::SubscriptionData& data) {
        callbacks_ = &data.callbacks_;
        return std::make_unique<NiceMock<Envoy::Config::MockSubscription>>();
      }));

  EXPECT_TRUE(cluster_manager_->addOrUpdateCluster(
      parseClusterFromV3Yaml(eds_cluster_yaml, getEdsClusterType()), "version1"));

  // ClusterLoadAssignment should contain all hosts to be kept for the
  // cluster. If a host is not in a subsequent update it is removed.
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  addEndpoint(cluster_load_assignment, 1000);
  addEndpoint(cluster_load_assignment, 2000);
  addEndpoint(cluster_load_assignment, 3000);
  addEndpoint(cluster_load_assignment, 4000);
  auto decoded_resources = TestUtility::decodeResources({cluster_load_assignment}, "cluster_name");
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  cluster_load_assignment.clear_endpoints();
  addEndpoint(cluster_load_assignment, 1000);
  addEndpoint(cluster_load_assignment, 2000);
  decoded_resources = TestUtility::decodeResources({cluster_load_assignment}, "cluster_name");
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  EXPECT_LOG_CONTAINS("debug", "initializing TLS cluster cluster_1 inline",
                      cluster_manager_->getThreadLocalCluster("cluster_1"));
  EXPECT_EQ(readGauge("thread_local_cluster_manager.test_thread.clusters_inflated"), 1);
  Cluster& cluster = cluster_manager_->activeClusters().find("cluster_1")->second;
  EXPECT_EQ(cluster.info()->endpointStats().membership_total_.value(), 2);
  EXPECT_TRUE(
      hostsInHostsVector(cluster.prioritySet().hostSetsPerPriority()[0]->hosts(), {1000, 2000}));
}

// Test that removed hosts that were added again appear when initializing a deferred eds cluster.
TEST_P(EdsTest, ShouldHaveHostThatWasAddedAfterRemoval) {
  const std::string bootstrap_yaml = R"EOF(
    static_resources:
      clusters:
      - name: eds
        connect_timeout: 0.250s
        lb_policy: ROUND_ROBIN
        load_assignment:
          cluster_name: bootstrap_cluster
          endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 60000
    )EOF";

  auto bootstrap = parseBootstrapFromV3YamlEnableDeferredCluster(bootstrap_yaml);
  create(bootstrap);
  EXPECT_EQ(readGauge("thread_local_cluster_manager.test_thread.clusters_inflated"), 0);

  const std::string eds_cluster_yaml = R"EOF(
      name: cluster_1
      connect_timeout: 0.25s
      lb_policy: ROUND_ROBIN
      eds_cluster_config:
        service_name: fare
        eds_config:
          api_config_source:
            api_type: REST
            cluster_names:
            - eds
            refresh_delay: 1s
    )EOF";

  EXPECT_CALL(factory_, create(_))
      .WillOnce(testing::Invoke([this](Config::ConfigSubscriptionFactory::SubscriptionData& data) {
        callbacks_ = &data.callbacks_;
        return std::make_unique<NiceMock<Envoy::Config::MockSubscription>>();
      }));

  EXPECT_TRUE(cluster_manager_->addOrUpdateCluster(
      parseClusterFromV3Yaml(eds_cluster_yaml, getEdsClusterType()), "version1"));

  // ClusterLoadAssignment should contain all hosts to be kept for the
  // cluster. If a host is not in a subsequent update it is removed.
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  addEndpoint(cluster_load_assignment, 1000);
  addEndpoint(cluster_load_assignment, 2000);
  addEndpoint(cluster_load_assignment, 3000);
  addEndpoint(cluster_load_assignment, 4000);
  auto decoded_resources = TestUtility::decodeResources({cluster_load_assignment}, "cluster_name");
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  cluster_load_assignment.clear_endpoints();
  addEndpoint(cluster_load_assignment, 1000);
  addEndpoint(cluster_load_assignment, 2000);
  decoded_resources = TestUtility::decodeResources({cluster_load_assignment}, "cluster_name");
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  addEndpoint(cluster_load_assignment, 3000);
  decoded_resources = TestUtility::decodeResources({cluster_load_assignment}, "cluster_name");
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  EXPECT_LOG_CONTAINS("debug", "initializing TLS cluster cluster_1 inline",
                      cluster_manager_->getThreadLocalCluster("cluster_1"));
  EXPECT_EQ(readGauge("thread_local_cluster_manager.test_thread.clusters_inflated"), 1);
  Cluster& cluster = cluster_manager_->activeClusters().find("cluster_1")->second;
  EXPECT_EQ(cluster.info()->endpointStats().membership_total_.value(), 3);
  EXPECT_TRUE(hostsInHostsVector(cluster.prioritySet().hostSetsPerPriority()[0]->hosts(),
                                 {1000, 2000, 3000}));
}

// Test merging multiple priorities for a deferred eds cluster.
TEST_P(EdsTest, MultiplePrioritiesShouldMergeCorrectly) {
  const std::string bootstrap_yaml = R"EOF(
    static_resources:
      clusters:
      - name: eds
        connect_timeout: 0.250s
        lb_policy: ROUND_ROBIN
        load_assignment:
          cluster_name: bootstrap_cluster
          endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 60000
    )EOF";

  auto bootstrap = parseBootstrapFromV3YamlEnableDeferredCluster(bootstrap_yaml);
  create(bootstrap);
  EXPECT_EQ(readGauge("thread_local_cluster_manager.test_thread.clusters_inflated"), 0);

  const std::string eds_cluster_yaml = R"EOF(
      name: cluster_1
      connect_timeout: 0.25s
      lb_policy: ROUND_ROBIN
      eds_cluster_config:
        service_name: fare
        eds_config:
          api_config_source:
            api_type: REST
            cluster_names:
            - eds
            refresh_delay: 1s
    )EOF";

  EXPECT_CALL(factory_, create(_))
      .WillOnce(testing::Invoke([this](Config::ConfigSubscriptionFactory::SubscriptionData& data) {
        callbacks_ = &data.callbacks_;
        return std::make_unique<NiceMock<Envoy::Config::MockSubscription>>();
      }));

  EXPECT_TRUE(cluster_manager_->addOrUpdateCluster(
      parseClusterFromV3Yaml(eds_cluster_yaml, getEdsClusterType()), "version1"));

  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  addEndpoint(cluster_load_assignment, 1000);
  addEndpoint(cluster_load_assignment, 2000);
  addEndpoint(cluster_load_assignment, 3000, 2);
  auto decoded_resources = TestUtility::decodeResources({cluster_load_assignment}, "cluster_name");
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  cluster_load_assignment.clear_endpoints();
  addEndpoint(cluster_load_assignment, 1000);
  addEndpoint(cluster_load_assignment, 4000);
  addEndpoint(cluster_load_assignment, 5000, 1);
  decoded_resources = TestUtility::decodeResources({cluster_load_assignment}, "cluster_name");
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  EXPECT_LOG_CONTAINS("debug", "initializing TLS cluster cluster_1 inline",
                      cluster_manager_->getThreadLocalCluster("cluster_1"));
  EXPECT_EQ(readGauge("thread_local_cluster_manager.test_thread.clusters_inflated"), 1);
  Cluster& cluster = cluster_manager_->activeClusters().find("cluster_1")->second;
  EXPECT_EQ(cluster.prioritySet().hostSetsPerPriority().size(), 3);
  EXPECT_TRUE(
      hostsInHostsVector(cluster.prioritySet().hostSetsPerPriority()[0]->hosts(), {1000, 4000}));
  EXPECT_TRUE(hostsInHostsVector(cluster.prioritySet().hostSetsPerPriority()[1]->hosts(), {5000}));
  EXPECT_TRUE(cluster.prioritySet().hostSetsPerPriority()[2]->hosts().empty());
}

// Test updating an initialized deferred eds cluster.
TEST_P(EdsTest, ActiveClusterGetsUpdated) {
  const std::string bootstrap_yaml = R"EOF(
    static_resources:
      clusters:
      - name: eds
        connect_timeout: 0.250s
        lb_policy: ROUND_ROBIN
        load_assignment:
          cluster_name: bootstrap_cluster
          endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 60000
    )EOF";

  auto bootstrap = parseBootstrapFromV3YamlEnableDeferredCluster(bootstrap_yaml);
  create(bootstrap);
  EXPECT_EQ(readGauge("thread_local_cluster_manager.test_thread.clusters_inflated"), 0);

  const std::string eds_cluster_yaml = R"EOF(
      name: cluster_1
      connect_timeout: 0.25s
      lb_policy: ROUND_ROBIN
      eds_cluster_config:
        service_name: fare
        eds_config:
          api_config_source:
            api_type: REST
            cluster_names:
            - eds
            refresh_delay: 1s
    )EOF";

  EXPECT_CALL(factory_, create(_))
      .WillOnce(testing::Invoke([this](Config::ConfigSubscriptionFactory::SubscriptionData& data) {
        callbacks_ = &data.callbacks_;
        return std::make_unique<NiceMock<Envoy::Config::MockSubscription>>();
      }));

  EXPECT_TRUE(cluster_manager_->addOrUpdateCluster(
      parseClusterFromV3Yaml(eds_cluster_yaml, getEdsClusterType()), "version1"));

  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  cluster_load_assignment.set_cluster_name("fare");
  addEndpoint(cluster_load_assignment, 1000);
  addEndpoint(cluster_load_assignment, 2000);
  addEndpoint(cluster_load_assignment, 3000);
  auto decoded_resources = TestUtility::decodeResources({cluster_load_assignment}, "cluster_name");
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);

  EXPECT_LOG_CONTAINS("debug", "initializing TLS cluster cluster_1 inline",
                      cluster_manager_->getThreadLocalCluster("cluster_1"));
  EXPECT_EQ(readGauge("thread_local_cluster_manager.test_thread.clusters_inflated"), 1);
  Cluster& cluster = cluster_manager_->activeClusters().find("cluster_1")->second;
  EXPECT_TRUE(
      hostsInHostsVector(cluster.prioritySet().hostSetsPerPriority()[0]->hosts(), {1000, 2000}));

  cluster_load_assignment.clear_endpoints();
  addEndpoint(cluster_load_assignment, 1000);
  addEndpoint(cluster_load_assignment, 4000);
  decoded_resources = TestUtility::decodeResources({cluster_load_assignment}, "cluster_name");
  doOnConfigUpdateVerifyNoThrow(cluster_load_assignment);
  EXPECT_TRUE(
      hostsInHostsVector(cluster.prioritySet().hostSetsPerPriority()[0]->hosts(), {1000, 4000}));
}

} // namespace
} // namespace Upstream
} // namespace Envoy
