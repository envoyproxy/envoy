#include "test/common/upstream/cluster_manager_impl_test_common.h"
#include "test/mocks/upstream/cds_api.h"
#include "test/mocks/upstream/cluster_real_priority_set.h"
#include "test/mocks/upstream/cluster_update_callbacks.h"
#include "test/mocks/upstream/health_checker.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::Mock;
using ::testing::Return;
using ::testing::ReturnNew;
using ::testing::SaveArg;

class ClusterManagerLifecycleTest : public ClusterManagerImplTest,
                                    public testing::WithParamInterface<bool> {
protected:
  void create(const Bootstrap& bootstrap) override {
    if (useDeferredCluster()) {
      auto bootstrap_with_deferred_cluster = bootstrap;
      bootstrap_with_deferred_cluster.mutable_cluster_manager()
          ->set_enable_deferred_cluster_creation(true);
      ClusterManagerImplTest::create(bootstrap_with_deferred_cluster);
    } else {
      ClusterManagerImplTest::create(bootstrap);
    }
  }
  bool useDeferredCluster() const { return GetParam(); }
};

INSTANTIATE_TEST_SUITE_P(ClusterManagerLifecycleTest, ClusterManagerLifecycleTest, testing::Bool());

TEST_P(ClusterManagerLifecycleTest, ShutdownOrder) {
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
  EXPECT_EQ(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0],
            cluster_manager_->getThreadLocalCluster("cluster_1")
                ->loadBalancer()
                .chooseHost(nullptr)
                .host);

  // Local reference, primary reference, thread local reference, host reference
  if (useDeferredCluster()) {
    // Additional reference in Cluster Initialization Object.
    EXPECT_EQ(5U, cluster.info().use_count());
  } else {
    EXPECT_EQ(4U, cluster.info().use_count());
  }

  // Thread local reference should be gone.
  factory_.tls_.shutdownThread();
  if (useDeferredCluster()) {
    // Additional reference in Cluster Initialization Object.
    EXPECT_EQ(4U, cluster.info().use_count());
  } else {
    EXPECT_EQ(3U, cluster.info().use_count());
  }
}

TEST_P(ClusterManagerLifecycleTest, InitializeOrder) {
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
  cluster2->info_->lb_factory_ =
      Config::Utility::getFactoryByName<Upstream::TypedLoadBalancerFactory>(
          "envoy.load_balancing_policies.ring_hash");
  auto proto_message = cluster2->info_->lb_factory_->createEmptyConfigProto();
  cluster2->info_->typed_lb_config_ =
      cluster2->info_->lb_factory_->loadConfig(factory_.server_context_, *proto_message).value();

  // This part tests static init.
  InSequence s;
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _))
      .WillOnce(Return(std::make_pair(cds_cluster, nullptr)));
  ON_CALL(*cds_cluster, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _))
      .WillOnce(Return(std::make_pair(cluster1, nullptr)));
  ON_CALL(*cluster1, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _))
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

  EXPECT_CALL(factory_, clusterFromProto_(_, _, _))
      .WillOnce(Return(std::make_pair(cluster3, nullptr)));
  ON_CALL(*cluster3, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Secondary));
  ASSERT_TRUE(
      cluster_manager_->addOrUpdateCluster(defaultStaticCluster("cluster3"), "version1").ok());

  EXPECT_CALL(factory_, clusterFromProto_(_, _, _))
      .WillOnce(Return(std::make_pair(cluster4, nullptr)));
  ON_CALL(*cluster4, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(*cluster4, initialize(_));
  ASSERT_TRUE(
      cluster_manager_->addOrUpdateCluster(defaultStaticCluster("cluster4"), "version2").ok());

  EXPECT_CALL(factory_, clusterFromProto_(_, _, _))
      .WillOnce(Return(std::make_pair(cluster5, nullptr)));
  ON_CALL(*cluster5, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Secondary));
  ASSERT_TRUE(
      cluster_manager_->addOrUpdateCluster(defaultStaticCluster("cluster5"), "version3").ok());

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

TEST_P(ClusterManagerLifecycleTest, DynamicRemoveWithLocalCluster) {
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
  EXPECT_CALL(factory_, clusterFromProto_(_, _, false))
      .WillOnce(Return(std::make_pair(foo, nullptr)));
  ON_CALL(*foo, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(*foo, initialize(_));

  create(parseBootstrapFromV3Json(json));
  foo->initialize_callback_();

  // Now add a dynamic cluster. This cluster will have a member update callback from the local
  // cluster in its load balancer.
  std::shared_ptr<MockClusterMockPrioritySet> cluster1(new NiceMock<MockClusterMockPrioritySet>());
  cluster1->info_->name_ = "cluster1";
  EXPECT_CALL(factory_, clusterFromProto_(_, _, true))
      .WillOnce(Return(std::make_pair(cluster1, nullptr)));
  ON_CALL(*cluster1, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(*cluster1, initialize(_));
  ASSERT_TRUE(cluster_manager_->addOrUpdateCluster(defaultStaticCluster("cluster1"), "").ok());

  // Add another update callback on foo so we make sure callbacks keep working.
  ReadyWatcher membership_updated;
  auto priority_update_cb = foo->prioritySet().addPriorityUpdateCb(
      [&membership_updated](uint32_t, const HostVector&, const HostVector&) {
        membership_updated.ready();
        return absl::OkStatus();
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

TEST_P(ClusterManagerLifecycleTest, RemoveWarmingCluster) {
  time_system_.setSystemTime(std::chrono::milliseconds(1234567891234));
  create(defaultConfig());

  InSequence s;
  ReadyWatcher initialized;
  EXPECT_CALL(initialized, ready());
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

  std::shared_ptr<MockClusterMockPrioritySet> cluster1(new NiceMock<MockClusterMockPrioritySet>());
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _))
      .WillOnce(Return(std::make_pair(cluster1, nullptr)));
  EXPECT_CALL(*cluster1, initializePhase()).Times(0);
  EXPECT_CALL(*cluster1, initialize(_));
  EXPECT_TRUE(
      *cluster_manager_->addOrUpdateCluster(defaultStaticCluster("fake_cluster"), "version3"));
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

TEST_P(ClusterManagerLifecycleTest, TestModifyWarmingClusterDuringInitialization) {
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
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _))
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
    EXPECT_CALL(factory_, clusterFromProto_(_, _, _));
    EXPECT_TRUE(*cluster_manager_->addOrUpdateCluster(parseClusterFromV3Yaml(warming_cluster_yaml),
                                                      "warming"));

    // Mark all the rest of the clusters ready. Now the only warming cluster is the above one.
    EXPECT_CALL(cm_initialized, ready()).Times(0);
    cds_cluster->initialize_callback_();
  }

  {
    SCOPED_TRACE("Modify the only warming primary cluster to immediate ready.");
    EXPECT_CALL(factory_, clusterFromProto_(_, _, _));
    EXPECT_CALL(*cds, initialize());
    EXPECT_TRUE(
        *cluster_manager_->addOrUpdateCluster(parseClusterFromV3Yaml(ready_cluster_yaml), "ready"));
  }
  {
    SCOPED_TRACE("All clusters are ready.");
    EXPECT_CALL(cm_initialized, ready());
    cds->initialized_callback_();
  }
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cds_cluster.get()));
}

TEST_P(ClusterManagerLifecycleTest, ModifyWarmingCluster) {
  time_system_.setSystemTime(std::chrono::milliseconds(1234567891234));
  create(defaultConfig());

  InSequence s;
  ReadyWatcher initialized;
  EXPECT_CALL(initialized, ready());
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

  // Add a "fake_cluster" in warming state.
  std::shared_ptr<MockClusterMockPrioritySet> cluster1 =
      std::make_shared<NiceMock<MockClusterMockPrioritySet>>();
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _))
      .WillOnce(Return(std::make_pair(cluster1, nullptr)));
  EXPECT_CALL(*cluster1, initializePhase()).Times(0);
  EXPECT_CALL(*cluster1, initialize(_));
  EXPECT_TRUE(
      *cluster_manager_->addOrUpdateCluster(defaultStaticCluster("fake_cluster"), "version3"));
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
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _))
      .WillOnce(Return(std::make_pair(cluster2, nullptr)));
  EXPECT_CALL(*cluster2, initializePhase()).Times(0);
  EXPECT_CALL(*cluster2, initialize(_));
  EXPECT_TRUE(*cluster_manager_->addOrUpdateCluster(
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
TEST_P(ClusterManagerLifecycleTest, TestRevertWarmingCluster) {
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
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _))
      .WillOnce(Return(std::make_pair(cluster1, nullptr)));
  EXPECT_CALL(*cluster1, initialize(_));
  checkStats(0 /*added*/, 0 /*modified*/, 0 /*removed*/, 0 /*active*/, 0 /*warming*/);

  ASSERT_TRUE(
      cluster_manager_->addOrUpdateCluster(parseClusterFromV3Json(cluster_json1), "version1").ok());
  checkStats(1 /*added*/, 0 /*modified*/, 0 /*removed*/, 0 /*active*/, 1 /*warming*/);

  cluster1->initialize_callback_();
  checkStats(1 /*added*/, 0 /*modified*/, 0 /*removed*/, 1 /*active*/, 0 /*warming*/);

  // Start warming version2.
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _))
      .WillOnce(Return(std::make_pair(cluster2, nullptr)));
  EXPECT_CALL(*cluster2, initialize(_));
  ASSERT_TRUE(
      cluster_manager_->addOrUpdateCluster(parseClusterFromV3Json(cluster_json2), "version2").ok());
  checkStats(1 /*added*/, 1 /*modified*/, 0 /*removed*/, 1 /*active*/, 1 /*warming*/);

  // Start warming version3 instead, which is the same as version1.
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _))
      .WillOnce(Return(std::make_pair(cluster3, nullptr)));
  EXPECT_CALL(*cluster3, initialize(_));
  ASSERT_TRUE(
      cluster_manager_->addOrUpdateCluster(parseClusterFromV3Json(cluster_json1), "version3").ok());
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
TEST_P(ClusterManagerLifecycleTest, ShutdownWithWarming) {
  create(defaultConfig());

  InSequence s;
  ReadyWatcher initialized;
  EXPECT_CALL(initialized, ready());
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

  std::shared_ptr<MockClusterMockPrioritySet> cluster1(new NiceMock<MockClusterMockPrioritySet>());
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _))
      .WillOnce(Return(std::make_pair(cluster1, nullptr)));
  EXPECT_CALL(*cluster1, initializePhase()).Times(0);
  EXPECT_CALL(*cluster1, initialize(_));
  EXPECT_TRUE(
      *cluster_manager_->addOrUpdateCluster(defaultStaticCluster("fake_cluster"), "version1"));
  checkStats(1 /*added*/, 0 /*modified*/, 0 /*removed*/, 0 /*active*/, 1 /*warming*/);
  cluster_manager_->shutdown();
  checkStats(1 /*added*/, 0 /*modified*/, 0 /*removed*/, 0 /*active*/, 0 /*warming*/);

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster1.get()));
}

TEST_P(ClusterManagerLifecycleTest, DynamicAddRemove) {
  create(defaultConfig());

  InSequence s;
  ReadyWatcher initialized;
  EXPECT_CALL(initialized, ready());
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

  std::unique_ptr<MockClusterUpdateCallbacks> callbacks(new NiceMock<MockClusterUpdateCallbacks>());
  ClusterUpdateCallbacksHandlePtr cb =
      cluster_manager_->addThreadLocalClusterUpdateCallbacks(*callbacks);

  std::shared_ptr<MockClusterMockPrioritySet> cluster1(new NiceMock<MockClusterMockPrioritySet>());
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _))
      .WillOnce(Return(std::make_pair(cluster1, nullptr)));
  EXPECT_CALL(*cluster1, initializePhase()).Times(0);
  EXPECT_CALL(*cluster1, initialize(_));
  EXPECT_CALL(*callbacks, onClusterAddOrUpdate(_, _));
  EXPECT_TRUE(*cluster_manager_->addOrUpdateCluster(defaultStaticCluster("fake_cluster"), ""));
  checkStats(1 /*added*/, 0 /*modified*/, 0 /*removed*/, 0 /*active*/, 1 /*warming*/);
  EXPECT_EQ(1, cluster_manager_->warmingClusterCount());
  EXPECT_EQ(nullptr, cluster_manager_->getThreadLocalCluster("fake_cluster"));
  cluster1->initialize_callback_();

  EXPECT_EQ(cluster1->info_, cluster_manager_->getThreadLocalCluster("fake_cluster")->info());
  checkStats(1 /*added*/, 0 /*modified*/, 0 /*removed*/, 1 /*active*/, 0 /*warming*/);
  EXPECT_EQ(0, cluster_manager_->warmingClusterCount());

  // Now try to update again but with the same hash.
  EXPECT_FALSE(*cluster_manager_->addOrUpdateCluster(defaultStaticCluster("fake_cluster"), ""));

  // Now do it again with a different hash.
  auto update_cluster = defaultStaticCluster("fake_cluster");
  update_cluster.mutable_per_connection_buffer_limit_bytes()->set_value(12345);

  std::shared_ptr<MockClusterMockPrioritySet> cluster2(new NiceMock<MockClusterMockPrioritySet>());
  cluster2->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster2->info_, "tcp://127.0.0.1:80")};
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _))
      .WillOnce(Return(std::make_pair(cluster2, nullptr)));
  EXPECT_CALL(*cluster2, initializePhase()).Times(0);
  EXPECT_CALL(*cluster2, initialize(_))
      .WillOnce(Invoke([cluster1](std::function<void()> initialize_callback) {
        // Test inline init.
        initialize_callback();
      }));
  EXPECT_CALL(*callbacks, onClusterAddOrUpdate(_, _));
  EXPECT_TRUE(*cluster_manager_->addOrUpdateCluster(update_cluster, ""));

  EXPECT_EQ(cluster2->info_, cluster_manager_->getThreadLocalCluster("fake_cluster")->info());
  EXPECT_EQ(1UL, cluster_manager_->clusters().active_clusters_.size());
  Http::ConnectionPool::MockInstance* cp = new Http::ConnectionPool::MockInstance();
  Http::ConnectionPool::Instance::IdleCb idle_cb;
  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _)).WillOnce(Return(cp));
  EXPECT_CALL(*cp, addIdleCallback(_)).WillOnce(SaveArg<0>(&idle_cb));
  EXPECT_EQ(
      cp,
      HttpPoolDataPeer::getPool(
          cluster_manager_->getThreadLocalCluster("fake_cluster")
              ->httpConnPool(
                  cluster_manager_->getThreadLocalCluster("fake_cluster")->chooseHost(nullptr).host,
                  ResourcePriority::Default, Http::Protocol::Http11, nullptr)));

  Tcp::ConnectionPool::MockInstance* cp2 = new Tcp::ConnectionPool::MockInstance();
  Tcp::ConnectionPool::Instance::IdleCb idle_cb2;
  EXPECT_CALL(factory_, allocateTcpConnPool_(_)).WillOnce(Return(cp2));
  EXPECT_CALL(*cp2, addIdleCallback(_)).WillOnce(SaveArg<0>(&idle_cb2));
  EXPECT_EQ(cp2, TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("fake_cluster")
                                              ->tcpConnPool(ResourcePriority::Default, nullptr)));

  Network::MockClientConnection* connection = new NiceMock<Network::MockClientConnection>();
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
  EXPECT_CALL(*cp, drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete));
  EXPECT_CALL(*cp2, drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete));
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

// Validates that a callback can remove itself from the callbacks list.
TEST_P(ClusterManagerLifecycleTest, ClusterAddOrUpdateCallbackRemovalDuringIteration) {
  create(defaultConfig());

  InSequence s;
  ReadyWatcher initialized;
  EXPECT_CALL(initialized, ready());
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

  std::unique_ptr<MockClusterUpdateCallbacks> callbacks(new NiceMock<MockClusterUpdateCallbacks>());
  ClusterUpdateCallbacksHandlePtr cb =
      cluster_manager_->addThreadLocalClusterUpdateCallbacks(*callbacks);

  std::shared_ptr<MockClusterMockPrioritySet> cluster1(new NiceMock<MockClusterMockPrioritySet>());
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _))
      .WillOnce(Return(std::make_pair(cluster1, nullptr)));
  EXPECT_CALL(*cluster1, initializePhase()).Times(0);
  EXPECT_CALL(*cluster1, initialize(_));
  EXPECT_CALL(*callbacks, onClusterAddOrUpdate(_, _))
      .WillOnce(Invoke([&cb](absl::string_view, ThreadLocalClusterCommand&) {
        // This call will remove the callback from the list.
        cb.reset();
      }));
  EXPECT_TRUE(*cluster_manager_->addOrUpdateCluster(defaultStaticCluster("fake_cluster"), ""));
  checkStats(1 /*added*/, 0 /*modified*/, 0 /*removed*/, 0 /*active*/, 1 /*warming*/);
  EXPECT_EQ(1, cluster_manager_->warmingClusterCount());
  EXPECT_EQ(nullptr, cluster_manager_->getThreadLocalCluster("fake_cluster"));
  cluster1->initialize_callback_();

  EXPECT_EQ(cluster1->info_, cluster_manager_->getThreadLocalCluster("fake_cluster")->info());
  checkStats(1 /*added*/, 0 /*modified*/, 0 /*removed*/, 1 /*active*/, 0 /*warming*/);
  EXPECT_EQ(0, cluster_manager_->warmingClusterCount());

  // Now do it again with a different hash.
  auto update_cluster = defaultStaticCluster("fake_cluster");
  update_cluster.mutable_per_connection_buffer_limit_bytes()->set_value(12345);

  std::shared_ptr<MockClusterMockPrioritySet> cluster2(new NiceMock<MockClusterMockPrioritySet>());
  cluster2->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster2->info_, "tcp://127.0.0.1:80")};
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _))
      .WillOnce(Return(std::make_pair(cluster2, nullptr)));
  EXPECT_CALL(*cluster2, initializePhase()).Times(0);
  EXPECT_CALL(*cluster2, initialize(_))
      .WillOnce(Invoke([cluster1](std::function<void()> initialize_callback) {
        // Test inline init.
        initialize_callback();
      }));
  // There shouldn't be a call to onClusterAddOrUpdate on the callbacks as the
  // handler was removed.
  EXPECT_CALL(*callbacks, onClusterAddOrUpdate(_, _)).Times(0);
  EXPECT_TRUE(*cluster_manager_->addOrUpdateCluster(update_cluster, ""));

  checkStats(1 /*added*/, 1 /*modified*/, 0 /*removed*/, 1 /*active*/, 0 /*warming*/);

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster1.get()));
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster2.get()));
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(callbacks.get()));
}

TEST_P(ClusterManagerLifecycleTest, AddOrUpdateClusterStaticExists) {
  const std::string json = fmt::sprintf("{\"static_resources\":{%s}}",
                                        clustersJson({defaultStaticClusterJson("fake_cluster")}));
  std::shared_ptr<MockClusterMockPrioritySet> cluster1(new NiceMock<MockClusterMockPrioritySet>());
  InSequence s;
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _))
      .WillOnce(Return(std::make_pair(cluster1, nullptr)));
  ON_CALL(*cluster1, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(*cluster1, initialize(_));

  create(parseBootstrapFromV3Json(json));

  ReadyWatcher initialized;
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

  EXPECT_CALL(initialized, ready());
  cluster1->initialize_callback_();

  EXPECT_FALSE(*cluster_manager_->addOrUpdateCluster(defaultStaticCluster("fake_cluster"), ""));

  // Attempt to remove a static cluster.
  EXPECT_FALSE(cluster_manager_->removeCluster("fake_cluster"));

  factory_.tls_.shutdownThread();

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster1.get()));
}

// Verifies that we correctly propagate the host_set state to the TLS clusters.
TEST_P(ClusterManagerLifecycleTest, HostsPostedToTlsCluster) {
  const std::string json = fmt::sprintf("{\"static_resources\":{%s}}",
                                        clustersJson({defaultStaticClusterJson("fake_cluster")}));
  std::shared_ptr<MockClusterRealPrioritySet> cluster1(new NiceMock<MockClusterRealPrioritySet>());
  InSequence s;
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _))
      .WillOnce(Return(std::make_pair(cluster1, nullptr)));
  ON_CALL(*cluster1, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
  EXPECT_CALL(*cluster1, initialize(_));

  create(parseBootstrapFromV3Json(json));

  ReadyWatcher initialized;
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

  EXPECT_CALL(initialized, ready());
  cluster1->initialize_callback_();

  // Set up the HostSet with 1 healthy, 1 degraded and 1 unhealthy.
  HostSharedPtr host1 = makeTestHost(cluster1->info_, "tcp://127.0.0.1:80");
  host1->healthFlagSet(HostImpl::HealthFlag::DEGRADED_ACTIVE_HC);
  HostSharedPtr host2 = makeTestHost(cluster1->info_, "tcp://127.0.0.1:80");
  host2->healthFlagSet(HostImpl::HealthFlag::FAILED_ACTIVE_HC);
  HostSharedPtr host3 = makeTestHost(cluster1->info_, "tcp://127.0.0.1:80");

  HostVector hosts{host1, host2, host3};
  auto hosts_ptr = std::make_shared<HostVector>(hosts);

  cluster1->priority_set_.updateHosts(
      0, HostSetImpl::partitionHosts(hosts_ptr, HostsPerLocalityImpl::empty()), nullptr, hosts, {},
      123, true, 100);

  auto* tls_cluster = cluster_manager_->getThreadLocalCluster(cluster1->info_->name());

  EXPECT_EQ(1, tls_cluster->prioritySet().hostSetsPerPriority().size());
  EXPECT_EQ(1, tls_cluster->prioritySet().hostSetsPerPriority()[0]->degradedHosts().size());
  EXPECT_EQ(host1, tls_cluster->prioritySet().hostSetsPerPriority()[0]->degradedHosts()[0]);
  EXPECT_EQ(1, tls_cluster->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(host3, tls_cluster->prioritySet().hostSetsPerPriority()[0]->healthyHosts()[0]);
  EXPECT_EQ(3, tls_cluster->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_TRUE(tls_cluster->prioritySet().hostSetsPerPriority()[0]->weightedPriorityHealth());
  EXPECT_EQ(100, tls_cluster->prioritySet().hostSetsPerPriority()[0]->overprovisioningFactor());

  factory_.tls_.shutdownThread();

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster1.get()));
}

// Test that we close all HTTP connection pool connections when there is a host health failure.
TEST_P(ClusterManagerLifecycleTest, CloseHttpConnectionsOnHealthFailure) {
  const std::string json = fmt::sprintf("{\"static_resources\":{%s}}",
                                        clustersJson({defaultStaticClusterJson("some_cluster")}));
  std::shared_ptr<MockClusterMockPrioritySet> cluster1(new NiceMock<MockClusterMockPrioritySet>());
  cluster1->info_->name_ = "some_cluster";
  HostSharedPtr test_host = makeTestHost(cluster1->info_, "tcp://127.0.0.1:80");
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

    EXPECT_CALL(factory_, clusterFromProto_(_, _, _))
        .WillOnce(Return(std::make_pair(cluster1, nullptr)));
    EXPECT_CALL(health_checker, addHostCheckCompleteCb(_));
    EXPECT_CALL(outlier_detector, addChangedStateCb(_));
    EXPECT_CALL(*cluster1, initialize(_))
        .WillOnce(Invoke([cluster1](std::function<void()> initialize_callback) {
          // Test inline init.
          initialize_callback();
        }));
    create(parseBootstrapFromV3Json(json));

    EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _)).WillOnce(Return(cp1));
    cluster_manager_->getThreadLocalCluster("some_cluster")
        ->httpConnPool(
            cluster_manager_->getThreadLocalCluster("some_cluster")->chooseHost(nullptr).host,
            ResourcePriority::Default, Http::Protocol::Http11, nullptr);
  }
  {
    InSequence s;
    outlier_detector.runCallbacks(test_host);
    health_checker.runCallbacks(test_host, HealthTransition::Unchanged, HealthState::Unhealthy);

    EXPECT_CALL(*cp1,
                drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections));
    test_host->healthFlagSet(Host::HealthFlag::FAILED_OUTLIER_CHECK);
    outlier_detector.runCallbacks(test_host);

    EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _)).WillOnce(Return(cp2));
    cluster_manager_->getThreadLocalCluster("some_cluster")
        ->httpConnPool(
            cluster_manager_->getThreadLocalCluster("some_cluster")->chooseHost(nullptr).host,
            ResourcePriority::High, Http::Protocol::Http11, nullptr);
  }

  // Order of these calls is implementation dependent, so can't sequence them!
  EXPECT_CALL(*cp1,
              drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections));
  EXPECT_CALL(*cp2,
              drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections));
  test_host->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker.runCallbacks(test_host, HealthTransition::Changed, HealthState::Unhealthy);

  test_host->healthFlagClear(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  outlier_detector.runCallbacks(test_host);
  test_host->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker.runCallbacks(test_host, HealthTransition::Changed, HealthState::Healthy);

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster1.get()));
}

// Test that we drain or close all HTTP or TCP connection pool connections when there is a host
// health failure and 'CLOSE_CONNECTIONS_ON_HOST_HEALTH_FAILURE' set to true.
TEST_P(ClusterManagerLifecycleTest,
       CloseConnectionsOnHealthFailureWithCloseConnectionsOnHostHealthFailure) {
  const std::string json = fmt::sprintf("{\"static_resources\":{%s}}",
                                        clustersJson({defaultStaticClusterJson("some_cluster")}));
  std::shared_ptr<MockClusterMockPrioritySet> cluster1(new NiceMock<MockClusterMockPrioritySet>());
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

  Http::ConnectionPool::MockInstance* cp1 = new NiceMock<Http::ConnectionPool::MockInstance>();
  Tcp::ConnectionPool::MockInstance* cp2 = new NiceMock<Tcp::ConnectionPool::MockInstance>();

  InSequence s;

  EXPECT_CALL(factory_, clusterFromProto_(_, _, _))
      .WillOnce(Return(std::make_pair(cluster1, nullptr)));
  EXPECT_CALL(health_checker, addHostCheckCompleteCb(_));
  EXPECT_CALL(outlier_detector, addChangedStateCb(_));
  EXPECT_CALL(*cluster1, initialize(_))
      .WillOnce(Invoke([cluster1](std::function<void()> initialize_callback) {
        // Test inline init.
        initialize_callback();
      }));
  create(parseBootstrapFromV3Json(json));

  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _)).WillOnce(Return(cp1));
  cluster_manager_->getThreadLocalCluster("some_cluster")
      ->httpConnPool(test_host, ResourcePriority::Default, Http::Protocol::Http11, nullptr);

  EXPECT_CALL(factory_, allocateTcpConnPool_(_)).WillOnce(Return(cp2));
  cluster_manager_->getThreadLocalCluster("some_cluster")
      ->tcpConnPool(ResourcePriority::Default, nullptr);

  // Order of these calls is implementation dependent, so can't sequence them!
  EXPECT_CALL(*cp1,
              drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections));
  EXPECT_CALL(*cp2, closeConnections());
  test_host->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker.runCallbacks(test_host, HealthTransition::Changed, HealthState::Unhealthy);

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster1.get()));
}

// Test that we close all HTTP connection pool connections when there is a host health failure.
// Verify that the pool gets deleted if it is idle, and that a crash does not occur due to
// deleting a container while iterating through it (see `do_not_delete_` in
// `ClusterManagerImpl::ThreadLocalClusterManagerImpl::onHostHealthFailure()`).
TEST_P(ClusterManagerLifecycleTest, CloseHttpConnectionsAndDeletePoolOnHealthFailure) {
  const std::string json = fmt::sprintf("{\"static_resources\":{%s}}",
                                        clustersJson({defaultStaticClusterJson("some_cluster")}));
  std::shared_ptr<MockClusterMockPrioritySet> cluster1(new NiceMock<MockClusterMockPrioritySet>());
  cluster1->info_->name_ = "some_cluster";
  HostSharedPtr test_host = makeTestHost(cluster1->info_, "tcp://127.0.0.1:80");
  cluster1->prioritySet().getMockHostSet(0)->hosts_ = {test_host};
  ON_CALL(*cluster1, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));

  MockHealthChecker health_checker;
  ON_CALL(*cluster1, healthChecker()).WillByDefault(Return(&health_checker));

  Outlier::MockDetector outlier_detector;
  ON_CALL(*cluster1, outlierDetector()).WillByDefault(Return(&outlier_detector));

  Http::ConnectionPool::MockInstance* cp1 = new NiceMock<Http::ConnectionPool::MockInstance>();

  InSequence s;

  EXPECT_CALL(factory_, clusterFromProto_(_, _, _))
      .WillOnce(Return(std::make_pair(cluster1, nullptr)));
  EXPECT_CALL(health_checker, addHostCheckCompleteCb(_));
  EXPECT_CALL(outlier_detector, addChangedStateCb(_));
  EXPECT_CALL(*cluster1, initialize(_))
      .WillOnce(Invoke([cluster1](std::function<void()> initialize_callback) {
        // Test inline init.
        initialize_callback();
      }));
  create(parseBootstrapFromV3Json(json));

  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _)).WillOnce(Return(cp1));
  cluster_manager_->getThreadLocalCluster("some_cluster")
      ->httpConnPool(test_host, ResourcePriority::Default, Http::Protocol::Http11, nullptr);

  outlier_detector.runCallbacks(test_host);
  health_checker.runCallbacks(test_host, HealthTransition::Unchanged, HealthState::Unhealthy);

  EXPECT_CALL(*cp1,
              drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections))
      .WillOnce(Invoke([&]() { cp1->idle_cb_(); }));
  test_host->healthFlagSet(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  outlier_detector.runCallbacks(test_host);

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster1.get()));
}

// Test that we close all TCP connection pool connections when there is a host health failure.
TEST_P(ClusterManagerLifecycleTest, CloseTcpConnectionPoolsOnHealthFailure) {
  const std::string json = fmt::sprintf("{\"static_resources\":{%s}}",
                                        clustersJson({defaultStaticClusterJson("some_cluster")}));
  std::shared_ptr<MockClusterMockPrioritySet> cluster1(new NiceMock<MockClusterMockPrioritySet>());
  cluster1->info_->name_ = "some_cluster";
  HostSharedPtr test_host = makeTestHost(cluster1->info_, "tcp://127.0.0.1:80");
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

    EXPECT_CALL(factory_, clusterFromProto_(_, _, _))
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
    health_checker.runCallbacks(test_host, HealthTransition::Unchanged, HealthState::Unhealthy);

    EXPECT_CALL(*cp1,
                drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections));
    test_host->healthFlagSet(Host::HealthFlag::FAILED_OUTLIER_CHECK);
    outlier_detector.runCallbacks(test_host);

    EXPECT_CALL(factory_, allocateTcpConnPool_(_)).WillOnce(Return(cp2));
    cluster_manager_->getThreadLocalCluster("some_cluster")
        ->tcpConnPool(ResourcePriority::High, nullptr);
  }

  // Order of these calls is implementation dependent, so can't sequence them!
  EXPECT_CALL(*cp1,
              drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections));
  EXPECT_CALL(*cp2,
              drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections));
  test_host->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker.runCallbacks(test_host, HealthTransition::Changed, HealthState::Unhealthy);

  test_host->healthFlagClear(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  outlier_detector.runCallbacks(test_host);
  test_host->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker.runCallbacks(test_host, HealthTransition::Changed, HealthState::Healthy);

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster1.get()));
}

// Test that we close all TCP connection pool connections when there is a host health failure,
// when configured to do so.
TEST_P(ClusterManagerLifecycleTest, CloseTcpConnectionsOnHealthFailure) {
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

    EXPECT_CALL(factory_, clusterFromProto_(_, _, _))
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
    health_checker.runCallbacks(test_host, HealthTransition::Unchanged, HealthState::Unhealthy);

    EXPECT_CALL(*connection1, close(Network::ConnectionCloseType::NoFlush, _));
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
  EXPECT_CALL(*connection1, close(Network::ConnectionCloseType::NoFlush, _));
  EXPECT_CALL(*connection2, close(Network::ConnectionCloseType::NoFlush, _));
  test_host->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker.runCallbacks(test_host, HealthTransition::Changed, HealthState::Unhealthy);

  test_host->healthFlagClear(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  outlier_detector.runCallbacks(test_host);
  test_host->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker.runCallbacks(test_host, HealthTransition::Changed, HealthState::Healthy);

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster1.get()));
}

// Test that we do not close TCP connection pool connections when there is a host health failure,
// when not configured to do so.
TEST_P(ClusterManagerLifecycleTest, DoNotCloseTcpConnectionsOnHealthFailure) {
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
  HostSharedPtr test_host = makeTestHost(cluster1->info_, "tcp://127.0.0.1:80");
  cluster1->prioritySet().getMockHostSet(0)->hosts_ = {test_host};
  ON_CALL(*cluster1, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));

  MockHealthChecker health_checker;
  ON_CALL(*cluster1, healthChecker()).WillByDefault(Return(&health_checker));

  Outlier::MockDetector outlier_detector;
  ON_CALL(*cluster1, outlierDetector()).WillByDefault(Return(&outlier_detector));

  Network::MockClientConnection* connection1 = new NiceMock<Network::MockClientConnection>();
  Host::CreateConnectionData conn_info1;

  EXPECT_CALL(factory_, clusterFromProto_(_, _, _))
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
  health_checker.runCallbacks(test_host, HealthTransition::Unchanged, HealthState::Unhealthy);

  EXPECT_CALL(*connection1, close(_)).Times(0);
  test_host->healthFlagSet(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  outlier_detector.runCallbacks(test_host);

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(cluster1.get()));
}

TEST_P(ClusterManagerLifecycleTest, DynamicHostRemove) {
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
  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, _)).WillOnce(Return(dns_resolver));

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

  dns_callback(Network::DnsResolver::ResolutionStatus::Completed, "",
               TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2"}));

  // After we are initialized, we should immediately get called back if someone asks for an
  // initialize callback.
  EXPECT_CALL(initialized, ready());
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _))
      .Times(4)
      .WillRepeatedly(ReturnNew<NiceMock<Http::ConnectionPool::MockInstance>>());

  // This should provide us a CP for each of the above hosts.
  Http::ConnectionPool::MockInstance* cp1 = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::Default, Http::Protocol::Http11, nullptr));
  Http::ConnectionPool::MockInstance* cp2 = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::Default, Http::Protocol::Http11, nullptr));
  Http::ConnectionPool::MockInstance* cp1_high = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::High, Http::Protocol::Http11, nullptr));
  Http::ConnectionPool::MockInstance* cp2_high = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::High, Http::Protocol::Http11, nullptr));

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
  dns_callback(Network::DnsResolver::ResolutionStatus::Completed, "",
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
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::Default, Http::Protocol::Http11, nullptr));
  Http::ConnectionPool::MockInstance* cp3_high = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::High, Http::Protocol::Http11, nullptr));
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
  dns_callback(Network::DnsResolver::ResolutionStatus::Completed, "",
               // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
               TestUtility::makeDnsResponse({"127.0.0.2", "127.0.0.3"}));
  factory_.tls_.shutdownThread();
}

TEST_P(ClusterManagerLifecycleTest, DynamicHostRemoveWithTls) {
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
  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, _)).WillOnce(Return(dns_resolver));

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

  dns_callback(Network::DnsResolver::ResolutionStatus::Completed, "",
               TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2"}));

  // After we are initialized, we should immediately get called back if someone asks for an
  // initialize callback.
  EXPECT_CALL(initialized, ready());
  cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _))
      .Times(4)
      .WillRepeatedly(ReturnNew<NiceMock<Http::ConnectionPool::MockInstance>>());

  // This should provide us a CP for each of the above hosts.
  Http::ConnectionPool::MockInstance* cp1 = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::Default, Http::Protocol::Http11, nullptr));
  Http::ConnectionPool::MockInstance* cp2 = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::Default, Http::Protocol::Http11, nullptr));
  Http::ConnectionPool::MockInstance* cp1_high = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::High, Http::Protocol::Http11, nullptr));
  Http::ConnectionPool::MockInstance* cp2_high = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::High, Http::Protocol::Http11, nullptr));

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
  dns_callback(Network::DnsResolver::ResolutionStatus::Completed, "",
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
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::Default, Http::Protocol::Http11, nullptr));
  Http::ConnectionPool::MockInstance* cp3_high = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::High, Http::Protocol::Http11, nullptr));
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
  dns_callback(Network::DnsResolver::ResolutionStatus::Completed, "",
               // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
               TestUtility::makeDnsResponse({"127.0.0.2", "127.0.0.3"}));
  factory_.tls_.shutdownThread();
}

// Test that default DNS resolver with TCP lookups is used, when there are no DNS custom resolvers
// configured per cluster and `dns_resolver_options.use_tcp_for_dns_lookups` is set in bootstrap
// config.
TEST_P(ClusterManagerLifecycleTest, UseTcpInDefaultDnsResolver) {
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
  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, _)).Times(0);

  Network::DnsResolver::ResolveCb dns_callback;
  Network::MockActiveDnsQuery active_dns_query;
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&dns_callback), Return(&active_dns_query)));
  create(parseBootstrapFromV3Yaml(yaml));
  factory_.tls_.shutdownThread();
}

// Test that custom DNS resolver is used, when custom resolver is configured
// per cluster and deprecated field `dns_resolvers` is specified.
TEST_P(ClusterManagerLifecycleTest, CustomDnsResolverSpecifiedViaDeprecatedField) {
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
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  envoy::config::core::v3::Address resolvers;
  Network::Utility::addressToProtobufAddress(Network::Address::Ipv4Instance("1.2.3.4", 80),
                                             resolvers);
  cares.add_resolvers()->MergeFrom(resolvers);
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  typed_dns_resolver_config.mutable_typed_config()->PackFrom(cares);
  typed_dns_resolver_config.set_name(std::string(Network::CaresDnsResolver));
  // As custom resolver is specified via deprecated field `dns_resolvers` in clusters
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

// Test that custom DNS resolver is used, when custom resolver is configured
// per cluster and deprecated field `dns_resolvers` is specified with multiple resolvers.
TEST_P(ClusterManagerLifecycleTest, CustomDnsResolverSpecifiedViaDeprecatedFieldMultipleResolvers) {
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
  // As custom resolver is specified via deprecated field `dns_resolvers` in clusters
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

// This is a regression test for a use-after-free in
// ClusterManagerImpl::ThreadLocalClusterManagerImpl::drainConnPools(), where a removal at one
// priority from the ConnPoolsContainer would delete the ConnPoolsContainer mid-iteration over the
// pool.
TEST_P(ClusterManagerLifecycleTest, DynamicHostRemoveDefaultPriority) {
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
  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, _)).WillOnce(Return(dns_resolver));

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

  dns_callback(Network::DnsResolver::ResolutionStatus::Completed, "",
               TestUtility::makeDnsResponse({"127.0.0.2"}));

  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _))
      .WillOnce(ReturnNew<NiceMock<Http::ConnectionPool::MockInstance>>());

  EXPECT_CALL(factory_, allocateTcpConnPool_)
      .WillOnce(ReturnNew<NiceMock<Tcp::ConnectionPool::MockInstance>>());

  Http::ConnectionPool::MockInstance* cp = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::Default, Http::Protocol::Http11, nullptr));

  Tcp::ConnectionPool::MockInstance* tcp =
      TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                   ->tcpConnPool(ResourcePriority::Default, nullptr));

  // Immediate drain, since this can happen with the HTTP codecs.
  EXPECT_CALL(*cp, drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete))
      .WillOnce(Invoke([&]() {
        cp->idle_cb_();
        cp->idle_cb_ = nullptr;
      }));
  EXPECT_CALL(*tcp, drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete))
      .WillOnce(Invoke([&]() {
        tcp->idle_cb_();
        tcp->idle_cb_ = nullptr;
      }));

  // Remove the first host, this should lead to the cp being drained, without
  // crash.
  dns_timer_->invokeCallback();
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
  dns_callback(Network::DnsResolver::ResolutionStatus::Completed, "",
               TestUtility::makeDnsResponse({}));

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
TEST_P(ClusterManagerLifecycleTest, ConnPoolDestroyWithDraining) {
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
  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, _)).WillOnce(Return(dns_resolver));

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

  dns_callback(Network::DnsResolver::ResolutionStatus::Completed, "",
               TestUtility::makeDnsResponse({"127.0.0.2"}));

  MockConnPoolWithDestroy* mock_cp = new MockConnPoolWithDestroy();
  Http::ConnectionPool::Instance::IdleCb drained_cb;
  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _)).WillOnce(Return(mock_cp));
  EXPECT_CALL(*mock_cp, addIdleCallback(_)).WillOnce(SaveArg<0>(&drained_cb));
  EXPECT_CALL(*mock_cp, drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete));

  MockTcpConnPoolWithDestroy* mock_tcp = new NiceMock<MockTcpConnPoolWithDestroy>();
  Tcp::ConnectionPool::Instance::IdleCb tcp_drained_cb;
  EXPECT_CALL(factory_, allocateTcpConnPool_).WillOnce(Return(mock_tcp));
  EXPECT_CALL(*mock_tcp, addIdleCallback(_)).WillOnce(SaveArg<0>(&tcp_drained_cb));
  EXPECT_CALL(*mock_tcp, drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete));

  HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::Default, Http::Protocol::Http11, nullptr));

  TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                               ->tcpConnPool(ResourcePriority::Default, nullptr));

  // Remove the first host, this should lead to the cp being drained.
  dns_timer_->invokeCallback();
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
  dns_callback(Network::DnsResolver::ResolutionStatus::Completed, "",
               TestUtility::makeDnsResponse({}));

  // The drained callback might get called when the CP is being destroyed.
  EXPECT_CALL(*mock_cp, onDestroy()).WillOnce(Invoke(drained_cb));
  EXPECT_CALL(*mock_tcp, onDestroy()).WillOnce(Invoke(tcp_drained_cb));
  factory_.tls_.shutdownThread();
}

// Tests that all the HC/weight/metadata changes are delivered in one go, as long as
// there's no hosts changes in between.
// Also tests that if hosts are added/removed between mergeable updates, delivery will
// happen and the scheduled update will be cancelled.
TEST_P(ClusterManagerLifecycleTest, MergedUpdates) {
  // Ensure we see the right set of added/removed hosts on every call.
  EXPECT_CALL(local_cluster_update_, post(_, _, _))
      .WillOnce(Invoke([](uint32_t priority, const HostVector& hosts_added,
                          const HostVector& hosts_removed) -> void {
        // 1st add of the 2 static localhost endpoints.
        EXPECT_EQ(0, priority);
        EXPECT_EQ(2, hosts_added.size());
        EXPECT_EQ(0, hosts_removed.size());
      }))
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

  createWithLocalClusterUpdate();

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
      {}, hosts_added, hosts_removed, 123, absl::nullopt, absl::nullopt);
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());

  // These calls should be merged, since there are no added/removed hosts.
  hosts_removed.clear();
  cluster.prioritySet().updateHosts(
      0,
      updateHostsParams(hosts, hosts_per_locality,
                        std::make_shared<const HealthyHostVector>(*hosts), hosts_per_locality),
      {}, hosts_added, hosts_removed, 123, absl::nullopt, absl::nullopt);
  cluster.prioritySet().updateHosts(
      0,
      updateHostsParams(hosts, hosts_per_locality,
                        std::make_shared<const HealthyHostVector>(*hosts), hosts_per_locality),
      {}, hosts_added, hosts_removed, 123, absl::nullopt, absl::nullopt);
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
      {}, hosts_added, hosts_removed, 123, absl::nullopt, absl::nullopt);
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
      {}, hosts_added, hosts_removed, 123, absl::nullopt, absl::nullopt);

  (*hosts)[0]->healthFlagSet(Host::HealthFlag::FAILED_EDS_HEALTH);
  cluster.prioritySet().updateHosts(
      0,
      updateHostsParams(hosts, hosts_per_locality,
                        std::make_shared<const HealthyHostVector>(*hosts), hosts_per_locality),
      {}, hosts_added, hosts_removed, 123, absl::nullopt, absl::nullopt);

  (*hosts)[0]->weight(100);
  cluster.prioritySet().updateHosts(
      0,
      updateHostsParams(hosts, hosts_per_locality,
                        std::make_shared<const HealthyHostVector>(*hosts), hosts_per_locality),
      {}, hosts_added, hosts_removed, 123, absl::nullopt, absl::nullopt);

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
      {}, hosts_added, hosts_removed, 123, absl::nullopt, absl::nullopt);

  EXPECT_EQ(3, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());
}

// Tests that mergeable updates outside of a window get applied immediately.
TEST_P(ClusterManagerLifecycleTest, MergedUpdatesOutOfWindow) {
  // Ensure we see the right set of added/removed hosts on every call.
  EXPECT_CALL(local_cluster_update_, post(_, _, _))
      .WillOnce(Invoke([](uint32_t priority, const HostVector& hosts_added,
                          const HostVector& hosts_removed) -> void {
        // 2 static host endpoints on Bootstrap's cluster.
        EXPECT_EQ(0, priority);
        EXPECT_EQ(2, hosts_added.size());
        EXPECT_EQ(0, hosts_removed.size());
      }))
      .WillOnce(Invoke([](uint32_t priority, const HostVector& hosts_added,
                          const HostVector& hosts_removed) -> void {
        // HC update, immediately delivered.
        EXPECT_EQ(0, priority);
        EXPECT_EQ(0, hosts_added.size());
        EXPECT_EQ(0, hosts_removed.size());
      }));

  createWithLocalClusterUpdate();

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
      {}, hosts_added, hosts_removed, 123, absl::nullopt, absl::nullopt);
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.update_out_of_merge_window").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());
}

// Tests that mergeable updates inside of a window are not applied immediately.
TEST_P(ClusterManagerLifecycleTest, MergedUpdatesInsideWindow) {
  EXPECT_CALL(local_cluster_update_, post(_, _, _))
      .WillOnce(Invoke([](uint32_t priority, const HostVector& hosts_added,
                          const HostVector& hosts_removed) -> void {
        EXPECT_EQ(0, priority);
        EXPECT_EQ(2, hosts_added.size());
        EXPECT_EQ(0, hosts_removed.size());
      }));

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
      {}, hosts_added, hosts_removed, 123, absl::nullopt, absl::nullopt);
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_out_of_merge_window").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());
}

// Tests that mergeable updates outside of a window get applied immediately when
// merging is disabled, and that the counters are correct.
TEST_P(ClusterManagerLifecycleTest, MergedUpdatesOutOfWindowDisabled) {
  // Ensure we see the right set of added/removed hosts on every call.
  EXPECT_CALL(local_cluster_update_, post(_, _, _))
      .WillOnce(Invoke([](uint32_t priority, const HostVector& hosts_added,
                          const HostVector& hosts_removed) -> void {
        // 2 static host endpoints on Bootstrap's cluster.
        EXPECT_EQ(0, priority);
        EXPECT_EQ(2, hosts_added.size());
        EXPECT_EQ(0, hosts_removed.size());
      }))
      .WillOnce(Invoke([](uint32_t priority, const HostVector& hosts_added,
                          const HostVector& hosts_removed) -> void {
        // HC update, immediately delivered.
        EXPECT_EQ(0, priority);
        EXPECT_EQ(0, hosts_added.size());
        EXPECT_EQ(0, hosts_removed.size());
      }));

  createWithLocalClusterUpdate(false);

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
      {}, hosts_added, hosts_removed, 123, absl::nullopt, absl::nullopt);
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_out_of_merge_window").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());
}

TEST_P(ClusterManagerLifecycleTest, MergedUpdatesDestroyedOnUpdate) {
  // Ensure we see the right set of added/removed hosts on every call, for the
  // dynamically added/updated cluster.
  EXPECT_CALL(local_cluster_update_, post(_, _, _))
      .WillOnce(Invoke([](uint32_t priority, const HostVector& hosts_added,
                          const HostVector& hosts_removed) -> void {
        // 1st add, which adds 2 localhost static hosts at ports 11001 and 11002.
        EXPECT_EQ(0, priority);
        EXPECT_EQ(2, hosts_added.size());
        EXPECT_EQ(0, hosts_removed.size());
      }))
      .WillOnce(Invoke([](uint32_t priority, const HostVector& hosts_added,
                          const HostVector& hosts_removed) -> void {
        // 2nd add, when the dynamic `new_cluster` is added with static host 127.0.0.1:12001.
        EXPECT_EQ(0, priority);
        EXPECT_EQ(1, hosts_added.size());
        EXPECT_EQ(0, hosts_removed.size());
      }))
      .WillOnce(Invoke([](uint32_t priority, const HostVector& hosts_added,
                          const HostVector& hosts_removed) -> void {
        // 1st removal of the dynamic `new_cluster`.
        EXPECT_EQ(0, priority);
        EXPECT_EQ(0, hosts_added.size());
        EXPECT_EQ(1, hosts_removed.size());
      }));

  EXPECT_CALL(local_hosts_removed_, post(_)).WillOnce(Invoke([](const auto& hosts_removed) {
    // 1st removal of the dynamic `new_cluster`.
    EXPECT_EQ(1, hosts_removed.size());
  }));

  // We create the default cluster, although for this test we won't use it since
  // we can only update dynamic clusters.
  createWithLocalClusterUpdate();

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
  EXPECT_TRUE(*cluster_manager_->addOrUpdateCluster(parseClusterFromV3Yaml(yaml), "version1"));

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
      {}, hosts_added, hosts_removed, 123, absl::nullopt, absl::nullopt);
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());

  // These calls should be merged, since there are no added/removed hosts.
  hosts_removed.clear();
  cluster.prioritySet().updateHosts(
      0,
      updateHostsParams(hosts, hosts_per_locality,
                        std::make_shared<const HealthyHostVector>(*hosts), hosts_per_locality),
      {}, hosts_added, hosts_removed, 123, absl::nullopt, absl::nullopt);
  cluster.prioritySet().updateHosts(
      0,
      updateHostsParams(hosts, hosts_per_locality,
                        std::make_shared<const HealthyHostVector>(*hosts), hosts_per_locality),
      {}, hosts_added, hosts_removed, 123, absl::nullopt, absl::nullopt);
  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());

  // Update the cluster, which should cancel the pending updates.
  std::shared_ptr<MockClusterMockPrioritySet> updated(new NiceMock<MockClusterMockPrioritySet>());
  updated->info_->name_ = "new_cluster";
  EXPECT_CALL(factory_, clusterFromProto_(_, _, true))
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
      *cluster_manager_->addOrUpdateCluster(parseClusterFromV3Yaml(yaml_updated), "version2"));
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

// Test that the read only cross-priority host map in the main thread is correctly synchronized to
// the worker thread when the cluster's host set is updated.
TEST_P(ClusterManagerLifecycleTest, CrossPriorityHostMapSyncTest) {
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
      common_lb_config:
        update_merge_window: 0s
  )EOF";
  create(parseBootstrapFromV3Yaml(yaml));

  Cluster& cluster = cluster_manager_->activeClusters().begin()->second;
  EXPECT_EQ(2, cluster.prioritySet().crossPriorityHostMap()->size());
  EXPECT_EQ(
      cluster_manager_->getThreadLocalCluster("cluster_1")->prioritySet().crossPriorityHostMap(),
      cluster.prioritySet().crossPriorityHostMap());

  HostVectorSharedPtr hosts(
      new HostVector(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()));
  HostsPerLocalitySharedPtr hosts_per_locality = std::make_shared<HostsPerLocalityImpl>();
  HostVector hosts_added;
  HostVector hosts_removed;

  hosts_removed.push_back((*hosts)[0]);
  cluster.prioritySet().updateHosts(
      0,
      updateHostsParams(hosts, hosts_per_locality,
                        std::make_shared<const HealthyHostVector>(*hosts), hosts_per_locality),
      {}, hosts_added, hosts_removed, 123, absl::nullopt, absl::nullopt);

  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());

  EXPECT_EQ(1, cluster.prioritySet().crossPriorityHostMap()->size());
  EXPECT_EQ(
      cluster_manager_->getThreadLocalCluster("cluster_1")->prioritySet().crossPriorityHostMap(),
      cluster.prioritySet().crossPriorityHostMap());

  hosts_added.push_back((*hosts)[0]);
  hosts_removed.clear();
  cluster.prioritySet().updateHosts(
      0,
      updateHostsParams(hosts, hosts_per_locality,
                        std::make_shared<const HealthyHostVector>(*hosts), hosts_per_locality),
      {}, hosts_added, hosts_removed, 123, absl::nullopt, absl::nullopt);
  EXPECT_EQ(2, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());

  EXPECT_EQ(2, cluster.prioritySet().crossPriorityHostMap()->size());
  EXPECT_EQ(
      cluster_manager_->getThreadLocalCluster("cluster_1")->prioritySet().crossPriorityHostMap(),
      cluster.prioritySet().crossPriorityHostMap());
}

// Make sure the drainConnections() with a predicate can correctly exclude a host.
TEST_P(ClusterManagerLifecycleTest, DrainConnectionsPredicate) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
  )EOF";

  create(parseBootstrapFromV3Yaml(yaml));

  // Set up the HostSet.
  Cluster& cluster = cluster_manager_->activeClusters().begin()->second;
  HostSharedPtr host1 = makeTestHost(cluster.info(), "tcp://127.0.0.1:80");
  HostSharedPtr host2 = makeTestHost(cluster.info(), "tcp://127.0.0.1:81");

  HostVector hosts{host1, host2};
  auto hosts_ptr = std::make_shared<HostVector>(hosts);

  // Sending non-mergeable updates.
  cluster.prioritySet().updateHosts(
      0, HostSetImpl::partitionHosts(hosts_ptr, HostsPerLocalityImpl::empty()), nullptr, hosts, {},
      123, absl::nullopt, 100);

  // Using RR LB get a pool for each host.
  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _))
      .Times(2)
      .WillRepeatedly(ReturnNew<NiceMock<Http::ConnectionPool::MockInstance>>());
  Http::ConnectionPool::MockInstance* cp1 = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::Default, Http::Protocol::Http11, nullptr));
  Http::ConnectionPool::MockInstance* cp2 = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::Default, Http::Protocol::Http11, nullptr));
  EXPECT_NE(cp1, cp2);

  EXPECT_CALL(*cp1,
              drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections));
  EXPECT_CALL(*cp2, drainConnections(_)).Times(0);
  cluster_manager_->drainConnections("cluster_1", [](const Upstream::Host& host) {
    return host.address()->asString() == "127.0.0.1:80";
  });
}

TEST_P(ClusterManagerLifecycleTest, ConnPoolsDrainedOnHostSetChange) {
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
                ->httpConnPool(
                    cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
                    ResourcePriority::Default, Http::Protocol::Http11, nullptr));
  EXPECT_EQ(absl::nullopt, cluster_manager_->getThreadLocalCluster("cluster_1")
                               ->tcpConnPool(ResourcePriority::Default, nullptr));
  EXPECT_EQ(nullptr,
            cluster_manager_->getThreadLocalCluster("cluster_1")->tcpConn(nullptr).connection_);

  Cluster& cluster = cluster_manager_->activeClusters().begin()->second;

  // Set up the HostSet.
  HostSharedPtr host1 = makeTestHost(cluster.info(), "tcp://127.0.0.1:80");
  HostSharedPtr host2 = makeTestHost(cluster.info(), "tcp://127.0.0.1:81");

  HostVector hosts{host1, host2};
  auto hosts_ptr = std::make_shared<HostVector>(hosts);

  // Sending non-mergeable updates.
  cluster.prioritySet().updateHosts(
      0, HostSetImpl::partitionHosts(hosts_ptr, HostsPerLocalityImpl::empty()), nullptr, hosts, {},
      123, absl::nullopt, 100);

  EXPECT_EQ(1, factory_.stats_.counter("cluster_manager.cluster_updated").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.cluster_updated_via_merge").value());
  EXPECT_EQ(0, factory_.stats_.counter("cluster_manager.update_merge_cancelled").value());

  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _))
      .Times(3)
      .WillRepeatedly(ReturnNew<NiceMock<Http::ConnectionPool::MockInstance>>());

  EXPECT_CALL(factory_, allocateTcpConnPool_)
      .Times(3)
      .WillRepeatedly(ReturnNew<NiceMock<Tcp::ConnectionPool::MockInstance>>());

  // This should provide us a CP for each of the above hosts.
  Http::ConnectionPool::MockInstance* cp1 = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::Default, Http::Protocol::Http11, nullptr));
  // Create persistent connection for host2.
  Http::ConnectionPool::MockInstance* cp2 = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::Default, Http::Protocol::Http2, nullptr));

  Tcp::ConnectionPool::MockInstance* tcp1 =
      TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                   ->tcpConnPool(ResourcePriority::Default, nullptr));

  Tcp::ConnectionPool::MockInstance* tcp2 =
      TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                   ->tcpConnPool(ResourcePriority::Default, nullptr));

  EXPECT_NE(cp1, cp2);
  EXPECT_NE(tcp1, tcp2);

  EXPECT_CALL(*cp2, drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete))
      .WillOnce(Invoke([&]() {
        cp2->idle_cb_();
        cp2->idle_cb_ = nullptr;
      }));
  EXPECT_CALL(*cp1, drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete))
      .WillOnce(Invoke([&]() {
        cp1->idle_cb_();
        cp1->idle_cb_ = nullptr;
      }));
  EXPECT_CALL(*tcp1, drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete))
      .WillOnce(Invoke([&]() {
        tcp1->idle_cb_();
        tcp1->idle_cb_ = nullptr;
      }));
  EXPECT_CALL(*tcp2, drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete))
      .WillOnce(Invoke([&]() {
        tcp2->idle_cb_();
        tcp2->idle_cb_ = nullptr;
      }));

  HostVector hosts_removed;
  hosts_removed.push_back(host2);

  // This update should drain all connection pools (host1, host2).
  cluster.prioritySet().updateHosts(
      0, HostSetImpl::partitionHosts(hosts_ptr, HostsPerLocalityImpl::empty()), nullptr, {},
      hosts_removed, 123, absl::nullopt, 100);

  // Recreate connection pool for host1.
  cp1 = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::Default, Http::Protocol::Http11, nullptr));

  tcp1 = TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                      ->tcpConnPool(ResourcePriority::Default, nullptr));

  HostSharedPtr host3 = makeTestHost(cluster.info(), "tcp://127.0.0.1:82");

  HostVector hosts_added;
  hosts_added.push_back(host3);

  EXPECT_CALL(*cp1, drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete))
      .WillOnce(Invoke([&]() {
        cp1->idle_cb_();
        cp1->idle_cb_ = nullptr;
      }));
  EXPECT_CALL(*tcp1, drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete))
      .WillOnce(Invoke([&]() {
        tcp1->idle_cb_();
        tcp1->idle_cb_ = nullptr;
      }));

  // Adding host3 should drain connection pool for host1.
  cluster.prioritySet().updateHosts(
      0, HostSetImpl::partitionHosts(hosts_ptr, HostsPerLocalityImpl::empty()), nullptr,
      hosts_added, {}, 123, absl::nullopt, 100);
}

TEST_P(ClusterManagerLifecycleTest, ConnPoolsNotDrainedOnHostSetChange) {
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
  HostSharedPtr host1 = makeTestHost(cluster.info(), "tcp://127.0.0.1:80");

  HostVector hosts{host1};
  auto hosts_ptr = std::make_shared<HostVector>(hosts);

  // Sending non-mergeable updates.
  cluster.prioritySet().updateHosts(
      0, HostSetImpl::partitionHosts(hosts_ptr, HostsPerLocalityImpl::empty()), nullptr, hosts, {},
      123, absl::nullopt, 100);

  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _))
      .Times(1)
      .WillRepeatedly(ReturnNew<NiceMock<Http::ConnectionPool::MockInstance>>());

  EXPECT_CALL(factory_, allocateTcpConnPool_)
      .Times(1)
      .WillRepeatedly(ReturnNew<NiceMock<Tcp::ConnectionPool::MockInstance>>());

  // This should provide us a CP for each of the above hosts.
  Http::ConnectionPool::MockInstance* cp1 = HttpPoolDataPeer::getPool(
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::Default, Http::Protocol::Http11, nullptr));

  Tcp::ConnectionPool::MockInstance* tcp1 =
      TcpPoolDataPeer::getPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                   ->tcpConnPool(ResourcePriority::Default, nullptr));

  HostSharedPtr host2 = makeTestHost(cluster.info(), "tcp://127.0.0.1:82");
  HostVector hosts_added;
  hosts_added.push_back(host2);

  // No connection pools should be drained.
  EXPECT_CALL(*cp1,
              drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections))
      .Times(0);
  EXPECT_CALL(*tcp1,
              drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections))
      .Times(0);

  // No connection pools should be drained.
  cluster.prioritySet().updateHosts(
      0, HostSetImpl::partitionHosts(hosts_ptr, HostsPerLocalityImpl::empty()), nullptr,
      hosts_added, {}, 123, absl::nullopt, 100);
}

TEST_P(ClusterManagerLifecycleTest, ConnPoolsIdleDeleted) {
  TestScopedRuntime scoped_runtime;

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
  HostSharedPtr host1 = makeTestHost(cluster.info(), "tcp://127.0.0.1:80");

  HostVector hosts{host1};
  auto hosts_ptr = std::make_shared<HostVector>(hosts);

  // Sending non-mergeable updates.
  cluster.prioritySet().updateHosts(
      0, HostSetImpl::partitionHosts(hosts_ptr, HostsPerLocalityImpl::empty()), nullptr, hosts, {},
      123, absl::nullopt, 100);

  {
    auto* cp1 = new NiceMock<Http::ConnectionPool::MockInstance>();
    EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _)).WillOnce(Return(cp1));
    std::function<void()> idle_callback;
    EXPECT_CALL(*cp1, addIdleCallback(_)).WillOnce(SaveArg<0>(&idle_callback));

    EXPECT_EQ(
        cp1,
        HttpPoolDataPeer::getPool(
            cluster_manager_->getThreadLocalCluster("cluster_1")
                ->httpConnPool(
                    cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
                    ResourcePriority::Default, Http::Protocol::Http11, nullptr)));
    // Request the same pool again and verify that it produces the same output
    EXPECT_EQ(
        cp1,
        HttpPoolDataPeer::getPool(
            cluster_manager_->getThreadLocalCluster("cluster_1")
                ->httpConnPool(
                    cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
                    ResourcePriority::Default, Http::Protocol::Http11, nullptr)));

    // Trigger the idle callback so we remove the connection pool
    idle_callback();

    auto* cp2 = new NiceMock<Http::ConnectionPool::MockInstance>();
    EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _)).WillOnce(Return(cp2));
    EXPECT_CALL(*cp2, addIdleCallback(_));

    // This time we expect cp2 since cp1 will have been destroyed
    EXPECT_EQ(
        cp2,
        HttpPoolDataPeer::getPool(
            cluster_manager_->getThreadLocalCluster("cluster_1")
                ->httpConnPool(
                    cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
                    ResourcePriority::Default, Http::Protocol::Http11, nullptr)));
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

} // namespace
} // namespace Upstream
} // namespace Envoy
