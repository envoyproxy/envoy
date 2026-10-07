// Tests for the eager_preconnect_floor preconnect feature.

#include "source/common/network/transport_socket_options_impl.h"

#include "test/common/upstream/cluster_manager_impl_test_common.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/test_common/test_runtime.h"

namespace Envoy {
namespace Upstream {
namespace {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnNew;

class EagerPreconnectFloorTest : public ClusterManagerImplTest {
public:
  void SetUp() override {
    ClusterManagerImplTest::SetUp();
    ON_CALL(factory_.tls_.dispatcher_, createTimer_(_))
        .WillByDefault(testing::Invoke([](Event::TimerCb cb) {
          auto* timer = new NiceMock<Event::MockTimer>();
          ON_CALL(*timer, enableTimer(_, _))
              .WillByDefault([cb](std::chrono::milliseconds, const ScopeTrackedObject*) { cb(); });
          return timer;
        }));
  }

  void createWithMinConnections(const std::string& yaml) {
    // Floor maintenance may allocate conn pools during initial host setup; default-mock allocator.
    ON_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _))
        .WillByDefault(ReturnNew<NiceMock<Http::ConnectionPool::MockInstance>>());
    create(parseBootstrapFromV3Yaml(yaml));
  }
};

TEST_F(EagerPreconnectFloorTest, AllFieldsDefaulted) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.25s
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
  )EOF";

  createWithMinConnections(yaml);

  auto* cluster = cluster_manager_->getThreadLocalCluster("cluster_1");
  ASSERT_NE(nullptr, cluster);
  EXPECT_EQ(0, cluster->info()->eagerPreconnectFloor());
  EXPECT_EQ(3, cluster->info()->eagerPreconnectFloorFailureThreshold());

  factory_.tls_.shutdownThread();
}

TEST_F(EagerPreconnectFloorTest, AllFieldsConfigured) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.25s
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
      preconnect_policy:
        eager_preconnect_floor:
          value: 3
        eager_preconnect_floor_failure_threshold:
          value: 5
  )EOF";

  createWithMinConnections(yaml);

  auto* cluster = cluster_manager_->getThreadLocalCluster("cluster_1");
  ASSERT_NE(nullptr, cluster);
  EXPECT_EQ(3, cluster->info()->eagerPreconnectFloor());
  EXPECT_EQ(5, cluster->info()->eagerPreconnectFloorFailureThreshold());

  factory_.tls_.shutdownThread();
}

TEST_F(ClusterManagerImplTest, EagerPreconnectFloorIncompatibleWithPoolPerDownstreamConnection) {
  // The eager preconnect floor warms connections ahead of requests, but with
  // connection_pool_per_downstream_connection there is no shared pool to warm. Reject the combo.
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.25s
      type: STATIC
      lb_policy: ROUND_ROBIN
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
      preconnect_policy:
        eager_preconnect_floor:
          value: 1
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      create(parseBootstrapFromV3Yaml(yaml)), EnvoyException,
      "eager_preconnect_floor is incompatible with connection_pool_per_downstream_connection");
}

TEST_F(EagerPreconnectFloorTest, DoesNotRefillErasedPool) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.25s
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
      preconnect_policy:
        eager_preconnect_floor:
          value: 1
  )EOF";

  // Capture the idle callback registered by the cluster manager on the filled pool,
  // and count pool allocations.
  Http::ConnectionPool::Instance::IdleCb captured_idle_cb;
  uint32_t pools_allocated = 0;
  ON_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _))
      .WillByDefault([&captured_idle_cb, &pools_allocated](auto&&...) {
        ++pools_allocated;
        auto* pool = new NiceMock<Http::ConnectionPool::MockInstance>();
        ON_CALL(*pool, addIdleCallback(_))
            .WillByDefault([&captured_idle_cb](Http::ConnectionPool::Instance::IdleCb cb) {
              captured_idle_cb = std::move(cb);
            });
        return pool;
      });

  create(parseBootstrapFromV3Yaml(yaml));

  // Preconnect floor is established only after the cluster is used as HTTP, so
  // nothing is allocated at init.
  ASSERT_EQ(0, pools_allocated) << "no floor maintenance before the cluster is used as HTTP";

  // First HTTP use creates a pool and opens a bootstrap connection.
  auto* cluster = cluster_manager_->getThreadLocalCluster("cluster_1");
  ASSERT_NE(nullptr, cluster);
  const auto& hosts = cluster->prioritySet().hostSetsPerPriority()[0]->hosts();
  ASSERT_EQ(1, hosts.size());
  auto opt_pool = cluster->httpConnPool(hosts[0], ResourcePriority::Default, std::nullopt, nullptr);
  ASSERT_TRUE(opt_pool.has_value());
  ASSERT_EQ(1, pools_allocated);
  ASSERT_TRUE(static_cast<bool>(captured_idle_cb))
      << "cluster manager should have registered an idle callback on the filled pool";

  // Make the pool fully idle.
  captured_idle_cb();

  EXPECT_EQ(1, pools_allocated) << "pool erase must not recreate the floor";

  factory_.tls_.shutdownThread();
}

TEST_F(EagerPreconnectFloorTest, FloorReadinessIsPerWorkerNotClusterWide) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.25s
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
      preconnect_policy:
        eager_preconnect_floor:
          value: 1
  )EOF";

  auto* pool = new NiceMock<Http::ConnectionPool::MockInstance>();
  // This worker's pool has no ready connection.
  ON_CALL(*pool, hasReadyConnection()).WillByDefault(Return(false));
  ON_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _)).WillByDefault(Return(pool));

  create(parseBootstrapFromV3Yaml(yaml));

  auto* cluster = cluster_manager_->getThreadLocalCluster("cluster_1");
  ASSERT_NE(nullptr, cluster);
  const auto& hosts = cluster->prioritySet().hostSetsPerPriority()[0]->hosts();
  ASSERT_EQ(1, hosts.size());

  // Other workers already hold connections to the host, so the cluster-wide gauge is non-zero.
  hosts[0]->stats().cx_active_.set(5);

  // This worker's pool gets a bootstrap connection.
  EXPECT_CALL(*pool, maybePreconnect(1.0f)).WillOnce(Return(true));
  cluster->httpConnPool(hosts[0], ResourcePriority::Default, std::nullopt, nullptr);

  factory_.tls_.shutdownThread();
}

// With the runtime guard disabled, cluster manager does not open a bootstrap connection.
TEST_F(EagerPreconnectFloorTest, DisabledByRuntimeGuard) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.eager_preconnect_floor", "false"}});

  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.25s
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
      preconnect_policy:
        eager_preconnect_floor:
          value: 1
  )EOF";

  auto* pool = new NiceMock<Http::ConnectionPool::MockInstance>();
  ON_CALL(*pool, hasReadyConnection()).WillByDefault(Return(false));
  ON_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _)).WillByDefault(Return(pool));

  create(parseBootstrapFromV3Yaml(yaml));

  auto* cluster = cluster_manager_->getThreadLocalCluster("cluster_1");
  ASSERT_NE(nullptr, cluster);
  const auto& hosts = cluster->prioritySet().hostSetsPerPriority()[0]->hosts();
  ASSERT_EQ(1, hosts.size());

  // Preconnect floor is configured, but the guard is off, so no bootstrap connection is opened
  // and the template is not recorded for later fan-out.
  uint32_t tls_posts = 0;
  ON_CALL(factory_.tls_.dispatcher_, post(_)).WillByDefault([&](Event::PostCb cb) {
    ++tls_posts;
    cb();
  });
  uint32_t main_posts = 0;
  ON_CALL(factory_.dispatcher_, post(_)).WillByDefault([&](Event::PostCb cb) {
    ++main_posts;
    cb();
  });
  EXPECT_CALL(*pool, maybePreconnect(_)).Times(0);
  cluster->httpConnPool(hosts[0], ResourcePriority::Default, std::nullopt, nullptr);
  EXPECT_EQ(0, tls_posts);
  EXPECT_EQ(0, main_posts);

  factory_.tls_.shutdownThread();
}

// With no floor configured, cluster manager does not open a bootstrap connection.
TEST_F(EagerPreconnectFloorTest, SkippedWhenFloorUnset) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.25s
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
  )EOF";

  auto* pool = new NiceMock<Http::ConnectionPool::MockInstance>();
  ON_CALL(*pool, hasReadyConnection()).WillByDefault(Return(false));
  ON_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _)).WillByDefault(Return(pool));

  create(parseBootstrapFromV3Yaml(yaml));

  auto* cluster = cluster_manager_->getThreadLocalCluster("cluster_1");
  ASSERT_NE(nullptr, cluster);
  const auto& hosts = cluster->prioritySet().hostSetsPerPriority()[0]->hosts();
  ASSERT_EQ(1, hosts.size());

  // Preconnect floor is not configured, even though the guard is on, so no
  // bootstrap connection is opened.
  EXPECT_CALL(*pool, maybePreconnect(_)).Times(0);
  cluster->httpConnPool(hosts[0], ResourcePriority::Default, std::nullopt, nullptr);

  factory_.tls_.shutdownThread();
}

// Preconnect floor is established only after the cluster is used, so nothing is allocated at init.
TEST_F(EagerPreconnectFloorTest, NoPreconnectUntilPoolUsed) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.25s
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
      preconnect_policy:
        eager_preconnect_floor:
          value: 1
  )EOF";

  uint32_t pools_allocated = 0;
  ON_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _))
      .WillByDefault([&pools_allocated](auto&&...) {
        ++pools_allocated;
        return new NiceMock<Http::ConnectionPool::MockInstance>();
      });

  create(parseBootstrapFromV3Yaml(yaml));

  auto* cluster = cluster_manager_->getThreadLocalCluster("cluster_1");
  ASSERT_NE(nullptr, cluster);

  // No pool type has been used yet, so no bootstrap connection is opened.
  EXPECT_EQ(0, pools_allocated);

  factory_.tls_.shutdownThread();
}

// The cluster manager opens exactly one bootstrap connection regardless of the configured floor.
TEST_F(EagerPreconnectFloorTest, OpensSingleBootstrapConnection) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.25s
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
      preconnect_policy:
        eager_preconnect_floor:
          value: 2
  )EOF";

  auto* pool = new NiceMock<Http::ConnectionPool::MockInstance>();
  ON_CALL(*pool, hasReadyConnection()).WillByDefault(Return(false));
  ON_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _)).WillByDefault(Return(pool));

  create(parseBootstrapFromV3Yaml(yaml));

  auto* cluster = cluster_manager_->getThreadLocalCluster("cluster_1");
  ASSERT_NE(nullptr, cluster);
  const auto& hosts = cluster->prioritySet().hostSetsPerPriority()[0]->hosts();
  ASSERT_EQ(1, hosts.size());

  // Floor is 2, but the cluster manager opens a single bootstrap connection.
  EXPECT_CALL(*pool, maybePreconnect(1.0f)).WillOnce(Return(true));
  cluster->httpConnPool(hosts[0], ResourcePriority::Default, std::nullopt, nullptr);

  factory_.tls_.shutdownThread();
}

// First HTTP use marks the cluster as used and opens a bootstrap connection for each eligible host.
TEST_F(EagerPreconnectFloorTest, BootstrapsAllHostsOnFirstUse) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.25s
      type: STATIC
      lb_policy: ROUND_ROBIN
      load_assignment:
        cluster_name: cluster_1
        endpoints:
        - lb_endpoints:
          - endpoint: {address: {socket_address: {address: 127.0.0.1, port_value: 11001}}}
          - endpoint: {address: {socket_address: {address: 127.0.0.1, port_value: 11002}}}
      preconnect_policy:
        eager_preconnect_floor:
          value: 1
  )EOF";

  uint32_t preconnects = 0;
  ON_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _))
      .WillByDefault([&preconnects](auto&&...) {
        auto* pool = new NiceMock<Http::ConnectionPool::MockInstance>();
        ON_CALL(*pool, hasReadyConnection()).WillByDefault(Return(false));
        ON_CALL(*pool, maybePreconnect(1.0f)).WillByDefault([&preconnects] {
          ++preconnects;
          return true;
        });
        return pool;
      });

  create(parseBootstrapFromV3Yaml(yaml));

  auto* cluster = cluster_manager_->getThreadLocalCluster("cluster_1");
  ASSERT_NE(nullptr, cluster);
  const auto& hosts = cluster->prioritySet().hostSetsPerPriority()[0]->hosts();
  ASSERT_EQ(2, hosts.size());

  // Using one host opens a bootstrap connection for each eligible host.
  cluster->httpConnPool(hosts[0], ResourcePriority::Default, std::nullopt, nullptr);
  EXPECT_EQ(2, preconnects);

  factory_.tls_.shutdownThread();
}

// All current hosts are bootstrapped from a single dispatcher post, not one post per host.
TEST_F(EagerPreconnectFloorTest, AllHostsBootstrapIsOneDispatcherPost) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.25s
      type: STATIC
      lb_policy: ROUND_ROBIN
      load_assignment:
        cluster_name: cluster_1
        endpoints:
        - lb_endpoints:
          - endpoint: {address: {socket_address: {address: 127.0.0.1, port_value: 11001}}}
          - endpoint: {address: {socket_address: {address: 127.0.0.1, port_value: 11002}}}
      preconnect_policy:
        eager_preconnect_floor:
          value: 1
  )EOF";

  std::vector<Event::PostCb> posted;
  ON_CALL(factory_.tls_.dispatcher_, post(_)).WillByDefault([&](Event::PostCb cb) {
    posted.push_back(std::move(cb));
  });

  uint32_t preconnects = 0;
  ON_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _))
      .WillByDefault([&preconnects](auto&&...) {
        auto* pool = new NiceMock<Http::ConnectionPool::MockInstance>();
        ON_CALL(*pool, hasReadyConnection()).WillByDefault(Return(false));
        ON_CALL(*pool, maybePreconnect(1.0f)).WillByDefault([&preconnects] {
          ++preconnects;
          return true;
        });
        return pool;
      });

  create(parseBootstrapFromV3Yaml(yaml));
  auto* cluster = cluster_manager_->getThreadLocalCluster("cluster_1");
  ASSERT_NE(nullptr, cluster);

  cluster->httpConnPool(cluster->prioritySet().hostSetsPerPriority()[0]->hosts()[0],
                        ResourcePriority::Default, std::nullopt, nullptr);
  ASSERT_EQ(1u, posted.size()) << "must not enqueue one dispatcher task per host";
  posted[0]();
  EXPECT_EQ(2, preconnects);

  factory_.tls_.shutdownThread();
}

// First HTTP use posts to the main dispatcher so other workers can pick up the template.
TEST_F(EagerPreconnectFloorTest, FirstUseNotifiesMainDispatcher) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.25s
      type: STATIC
      lb_policy: ROUND_ROBIN
      load_assignment:
        cluster_name: cluster_1
        endpoints:
        - lb_endpoints:
          - endpoint: {address: {socket_address: {address: 127.0.0.1, port_value: 11001}}}
      preconnect_policy:
        eager_preconnect_floor:
          value: 1
  )EOF";

  ON_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _))
      .WillByDefault(ReturnNew<NiceMock<Http::ConnectionPool::MockInstance>>());

  create(parseBootstrapFromV3Yaml(yaml));

  uint32_t main_posts = 0;
  ON_CALL(factory_.dispatcher_, post(_)).WillByDefault([&](Event::PostCb cb) {
    ++main_posts;
    cb();
  });

  auto* cluster = cluster_manager_->getThreadLocalCluster("cluster_1");
  ASSERT_NE(nullptr, cluster);
  cluster->httpConnPool(cluster->prioritySet().hostSetsPerPriority()[0]->hosts()[0],
                        ResourcePriority::Default, std::nullopt, nullptr);
  EXPECT_GE(main_posts, 1u) << "postThreadLocalHttpConnPoolUsed is posted to the main dispatcher";

  factory_.tls_.shutdownThread();
}

// Default and High are distinct templates; each gets a floor bootstrap on the same host.
TEST_F(EagerPreconnectFloorTest, BootstrapsMultipleTemplates) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.25s
      type: STATIC
      lb_policy: ROUND_ROBIN
      load_assignment:
        cluster_name: cluster_1
        endpoints:
        - lb_endpoints:
          - endpoint: {address: {socket_address: {address: 127.0.0.1, port_value: 11001}}}
      preconnect_policy:
        eager_preconnect_floor:
          value: 1
  )EOF";

  uint32_t preconnects = 0;
  ON_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _))
      .WillByDefault([&preconnects](auto&&...) {
        auto* pool = new NiceMock<Http::ConnectionPool::MockInstance>();
        ON_CALL(*pool, hasReadyConnection()).WillByDefault(Return(false));
        ON_CALL(*pool, maybePreconnect(1.0f)).WillByDefault([pool, &preconnects] {
          ++preconnects;
          ON_CALL(*pool, hasReadyConnection()).WillByDefault(Return(true));
          return true;
        });
        return pool;
      });

  create(parseBootstrapFromV3Yaml(yaml));
  auto* cluster = cluster_manager_->getThreadLocalCluster("cluster_1");
  ASSERT_NE(nullptr, cluster);
  const auto& hosts = cluster->prioritySet().hostSetsPerPriority()[0]->hosts();

  ASSERT_TRUE(cluster->httpConnPool(hosts[0], ResourcePriority::Default, std::nullopt, nullptr)
                  .has_value());
  EXPECT_EQ(1, preconnects);
  ASSERT_TRUE(
      cluster->httpConnPool(hosts[0], ResourcePriority::High, std::nullopt, nullptr).has_value());
  EXPECT_EQ(2, preconnects) << "High is a second template and must be bootstrapped separately";

  factory_.tls_.shutdownThread();
}

// Hosts that became ineligible for preconnect after being posted are skipped.
class BootstrapEligibilityRecheckTest : public EagerPreconnectFloorTest {
protected:
  static std::string yaml(uint32_t failure_threshold) {
    return absl::StrCat(R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.25s
      type: STATIC
      lb_policy: ROUND_ROBIN
      load_assignment:
        cluster_name: cluster_1
        endpoints:
        - lb_endpoints:
          - endpoint: {address: {socket_address: {address: 127.0.0.1, port_value: 11001}}}
      preconnect_policy:
        eager_preconnect_floor:
          value: 1
        eager_preconnect_floor_failure_threshold:
          value: )EOF",
                        failure_threshold, "\n");
  }

  std::vector<Event::PostCb> posted_;
  NiceMock<Http::ConnectionPool::MockInstance>* pool_ = nullptr;
  HostSharedPtr host_;

  void armAndCaptureBootstrap(uint32_t failure_threshold = 1) {
    ON_CALL(factory_.tls_.dispatcher_, post(_)).WillByDefault([this](Event::PostCb cb) {
      posted_.push_back(std::move(cb));
    });
    pool_ = new NiceMock<Http::ConnectionPool::MockInstance>();
    ON_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _)).WillByDefault(Return(pool_));

    create(parseBootstrapFromV3Yaml(yaml(failure_threshold)));
    auto* cluster = cluster_manager_->getThreadLocalCluster("cluster_1");
    ASSERT_NE(nullptr, cluster);
    host_ = cluster->prioritySet().hostSetsPerPriority()[0]->hosts()[0];

    // First HTTP use posts a bootstrap for the (eligible) host; capture it without running.
    cluster->httpConnPool(host_, ResourcePriority::Default, std::nullopt, nullptr);
    ASSERT_EQ(1u, posted_.size());
  }

  // Removes the single host from the cluster so that a captured bootstrap no longer targets a
  // current member.
  void removeSingleHostFromCluster() {
    Cluster& cluster = cluster_manager_->activeClusters().begin()->second;
    HostVector hosts_removed{host_};
    auto empty_hosts = std::make_shared<HostVector>();
    cluster.prioritySet().updateHosts(
        0, HostSetImpl::partitionHosts(empty_hosts, HostsPerLocalityImpl::empty()), nullptr, {},
        hosts_removed, std::nullopt, 100);
  }

  // Replaces the single host with a new Host instance at the same address.
  void replaceSingleHostWithSameAddress() {
    Cluster& cluster = cluster_manager_->activeClusters().begin()->second;
    HostVector hosts_removed{host_};
    HostSharedPtr new_host = makeTestHost(cluster.info(), "tcp://" + host_->address()->asString());
    auto new_hosts = std::make_shared<HostVector>(HostVector{new_host});
    auto* new_pool = new NiceMock<Http::ConnectionPool::MockInstance>();
    EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _)).WillOnce(Return(new_pool));
    cluster.prioritySet().updateHosts(
        0, HostSetImpl::partitionHosts(new_hosts, HostsPerLocalityImpl::empty()), nullptr,
        {new_host}, hosts_removed, std::nullopt, 100);
  }
};

TEST_F(BootstrapEligibilityRecheckTest, SkipsHostThatTurnedUnhealthy) {
  armAndCaptureBootstrap();
  host_->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  EXPECT_CALL(*pool_, maybePreconnect(_)).Times(0);
  posted_[0]();
  factory_.tls_.shutdownThread();
}

TEST_F(BootstrapEligibilityRecheckTest, SkipsHostThatGainedReadyConnection) {
  armAndCaptureBootstrap();
  ON_CALL(*pool_, hasReadyConnection()).WillByDefault(Return(true));
  EXPECT_CALL(*pool_, maybePreconnect(_)).Times(0);
  posted_[0]();
  factory_.tls_.shutdownThread();
}

TEST_F(BootstrapEligibilityRecheckTest, SkipsHostThatBecameUnreachable) {
  armAndCaptureBootstrap();
  host_->incConsecutiveEagerPreconnectFloorFailures();
  EXPECT_CALL(*pool_, maybePreconnect(_)).Times(0);
  posted_[0]();
  factory_.tls_.shutdownThread();
}

TEST_F(BootstrapEligibilityRecheckTest, SkipsHostThatLeftCluster) {
  armAndCaptureBootstrap();
  removeSingleHostFromCluster();
  EXPECT_CALL(*pool_, maybePreconnect(_)).Times(0);
  posted_[0]();
  factory_.tls_.shutdownThread();
}

TEST_F(BootstrapEligibilityRecheckTest, BootstrapsHostThatStayedEligible) {
  armAndCaptureBootstrap();
  EXPECT_CALL(*pool_, maybePreconnect(1.0f)).WillOnce(Return(true));
  posted_[0]();
  factory_.tls_.shutdownThread();
}

TEST_F(BootstrapEligibilityRecheckTest, BootstrapsHostWithFailuresBelowThreshold) {
  armAndCaptureBootstrap(/*failure_threshold=*/2);
  host_->incConsecutiveEagerPreconnectFloorFailures();
  EXPECT_CALL(*pool_, maybePreconnect(1.0f)).WillOnce(Return(true));
  posted_[0]();
  factory_.tls_.shutdownThread();
}

TEST_F(BootstrapEligibilityRecheckTest, SkipsReplacedHostWithSameAddress) {
  armAndCaptureBootstrap();
  replaceSingleHostWithSameAddress();
  EXPECT_CALL(*pool_, maybePreconnect(_)).Times(0);
  posted_[0]();
  factory_.tls_.shutdownThread();
}

TEST_F(EagerPreconnectFloorTest, BootstrapsHttpOnly) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.25s
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
      preconnect_policy:
        eager_preconnect_floor:
          value: 1
  )EOF";

  auto* http_pool = new NiceMock<Http::ConnectionPool::MockInstance>();
  ON_CALL(*http_pool, hasReadyConnection()).WillByDefault(Return(false));
  ON_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _)).WillByDefault(Return(http_pool));

  create(parseBootstrapFromV3Yaml(yaml));
  auto* cluster = cluster_manager_->getThreadLocalCluster("cluster_1");
  ASSERT_NE(nullptr, cluster);
  const auto& hosts = cluster->prioritySet().hostSetsPerPriority()[0]->hosts();
  ASSERT_EQ(1, hosts.size());

  EXPECT_CALL(*http_pool, maybePreconnect(1.0f)).Times(::testing::AtLeast(1));
  EXPECT_CALL(factory_, allocateTcpConnPool_(_)).Times(0);
  cluster->httpConnPool(hosts[0], ResourcePriority::Default, std::nullopt, nullptr);

  factory_.tls_.shutdownThread();
}

TEST_F(EagerPreconnectFloorTest, DoesNotBootstrapTcp) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.25s
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
      preconnect_policy:
        eager_preconnect_floor:
          value: 1
  )EOF";

  uint32_t http_pools = 0;
  ON_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _)).WillByDefault([&http_pools](auto&&...) {
    ++http_pools;
    return new NiceMock<Http::ConnectionPool::MockInstance>();
  });
  auto* tcp_pool = new NiceMock<Tcp::ConnectionPool::MockInstance>();
  ON_CALL(factory_, allocateTcpConnPool_(_)).WillByDefault(Return(tcp_pool));

  create(parseBootstrapFromV3Yaml(yaml));
  auto* cluster = cluster_manager_->getThreadLocalCluster("cluster_1");
  ASSERT_NE(nullptr, cluster);
  const auto& hosts = cluster->prioritySet().hostSetsPerPriority()[0]->hosts();
  ASSERT_EQ(1, hosts.size());

  EXPECT_CALL(*tcp_pool, maybePreconnect(_)).Times(0);
  cluster->tcpConnPool(hosts[0], ResourcePriority::Default, nullptr);
  EXPECT_EQ(0, http_pools);

  factory_.tls_.shutdownThread();
}

// First use with a TSO/options context must bootstrap that pool, not a nullptr-key ghost.
TEST_F(EagerPreconnectFloorTest, FloorDoesNotTargetNullptrKeyWhenContextHasTso) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.25s
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
      preconnect_policy:
        eager_preconnect_floor:
          value: 1
  )EOF";

  uint32_t pools_allocated = 0;
  ON_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _))
      .WillByDefault([&pools_allocated](auto&&...) {
        ++pools_allocated;
        auto* pool = new NiceMock<Http::ConnectionPool::MockInstance>();
        ON_CALL(*pool, hasReadyConnection()).WillByDefault(Return(false));
        ON_CALL(*pool, maybePreconnect(1.0f)).WillByDefault(Return(true));
        return pool;
      });

  create(parseBootstrapFromV3Yaml(yaml));
  auto* cluster = cluster_manager_->getThreadLocalCluster("cluster_1");
  ASSERT_NE(nullptr, cluster);
  const auto& hosts = cluster->prioritySet().hostSetsPerPriority()[0]->hosts();
  ASSERT_EQ(1, hosts.size());

  auto tso = std::make_shared<Network::TransportSocketOptionsImpl>("sni.example.com");
  NiceMock<MockLoadBalancerContext> tso_context;
  ON_CALL(tso_context, upstreamTransportSocketOptions()).WillByDefault(Return(tso));

  ASSERT_TRUE(cluster->httpConnPool(hosts[0], ResourcePriority::Default, std::nullopt, &tso_context)
                  .has_value());
  EXPECT_EQ(1, pools_allocated) << "floor bootstrap must reuse the request-shaped pool, not a "
                                   "nullptr-key ghost";

  ASSERT_TRUE(cluster->httpConnPool(hosts[0], ResourcePriority::Default, std::nullopt, nullptr)
                  .has_value());
  EXPECT_EQ(2, pools_allocated) << "a later nullptr-key lookup is a different pool";

  factory_.tls_.shutdownThread();
}

// CDS replace keeps http_conn_pool_params_ on ThreadLocalClusterManagerImpl across ClusterEntry
// recreation, so updateHosts can bootstrap from the remembered templates.
TEST_F(EagerPreconnectFloorTest, CopiesConnPoolParamsOnClusterReplace) {
  uint32_t preconnects = 0;
  ON_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _))
      .WillByDefault([&preconnects](auto&&...) {
        auto* pool = new NiceMock<Http::ConnectionPool::MockInstance>();
        ON_CALL(*pool, hasReadyConnection()).WillByDefault(Return(false));
        ON_CALL(*pool, maybePreconnect(1.0f)).WillByDefault([&preconnects] {
          ++preconnects;
          return true;
        });
        return pool;
      });

  create(defaultConfig());

  envoy::config::cluster::v3::Cluster cluster_v1;
  TestUtility::loadFromYaml(R"EOF(
    name: cluster_1
    connect_timeout: 0.25s
    type: STATIC
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: cluster_1
      endpoints:
      - lb_endpoints:
        - endpoint: {address: {socket_address: {address: 127.0.0.1, port_value: 11001}}}
    preconnect_policy:
      eager_preconnect_floor:
        value: 1
  )EOF",
                            cluster_v1);
  ASSERT_TRUE(*cluster_manager_->addOrUpdateCluster(cluster_v1, "v1"));

  auto* cluster = cluster_manager_->getThreadLocalCluster("cluster_1");
  ASSERT_NE(nullptr, cluster);
  const auto& hosts_v1 = cluster->prioritySet().hostSetsPerPriority()[0]->hosts();
  ASSERT_EQ(1, hosts_v1.size());
  ASSERT_TRUE(cluster->httpConnPool(hosts_v1[0], ResourcePriority::Default, std::nullopt, nullptr)
                  .has_value());
  EXPECT_EQ(1, preconnects);

  envoy::config::cluster::v3::Cluster cluster_v2;
  TestUtility::loadFromYaml(R"EOF(
    name: cluster_1
    connect_timeout: 0.25s
    type: STATIC
    lb_policy: ROUND_ROBIN
    per_connection_buffer_limit_bytes: 12345
    load_assignment:
      cluster_name: cluster_1
      endpoints:
      - lb_endpoints:
        - endpoint: {address: {socket_address: {address: 127.0.0.1, port_value: 11001}}}
        - endpoint: {address: {socket_address: {address: 127.0.0.1, port_value: 11002}}}
    preconnect_policy:
      eager_preconnect_floor:
        value: 1
  )EOF",
                            cluster_v2);
  ASSERT_TRUE(*cluster_manager_->addOrUpdateCluster(cluster_v2, "v2"));

  auto* cluster_after = cluster_manager_->getThreadLocalCluster("cluster_1");
  ASSERT_NE(nullptr, cluster_after);
  EXPECT_EQ(2, cluster_after->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(3, preconnects) << "remembered params must bootstrap both hosts after ClusterEntry "
                               "recreation (1 from first use + 2 after CDS replace)";

  factory_.tls_.shutdownThread();
}

TEST_F(EagerPreconnectFloorTest, ReturnsNullOptWhenHostIsNull) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.25s
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
  )EOF";
  createWithMinConnections(yaml);
  auto* cluster = cluster_manager_->getThreadLocalCluster("cluster_1");
  ASSERT_NE(nullptr, cluster);
  EXPECT_FALSE(
      cluster->httpConnPool(nullptr, ResourcePriority::Default, std::nullopt, nullptr).has_value());
  factory_.tls_.shutdownThread();
}

TEST_F(EagerPreconnectFloorTest, ErasesConnPoolParamsOnClusterRemove) {
  uint32_t preconnects = 0;
  ON_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _))
      .WillByDefault([&preconnects](auto&&...) {
        auto* pool = new NiceMock<Http::ConnectionPool::MockInstance>();
        ON_CALL(*pool, hasReadyConnection()).WillByDefault(Return(false));
        ON_CALL(*pool, maybePreconnect(1.0f)).WillByDefault([&preconnects] {
          ++preconnects;
          return true;
        });
        return pool;
      });

  create(defaultConfig());

  envoy::config::cluster::v3::Cluster cluster_v1;
  TestUtility::loadFromYaml(R"EOF(
    name: cluster_1
    connect_timeout: 0.25s
    type: STATIC
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: cluster_1
      endpoints:
      - lb_endpoints:
        - endpoint: {address: {socket_address: {address: 127.0.0.1, port_value: 11001}}}
    preconnect_policy:
      eager_preconnect_floor:
        value: 1
  )EOF",
                            cluster_v1);
  ASSERT_TRUE(*cluster_manager_->addOrUpdateCluster(cluster_v1, "v1"));

  auto* cluster = cluster_manager_->getThreadLocalCluster("cluster_1");
  ASSERT_NE(nullptr, cluster);
  const auto& hosts_v1 = cluster->prioritySet().hostSetsPerPriority()[0]->hosts();
  ASSERT_TRUE(cluster->httpConnPool(hosts_v1[0], ResourcePriority::Default, std::nullopt, nullptr)
                  .has_value());
  EXPECT_EQ(1, preconnects);

  // Remove the cluster.
  EXPECT_TRUE(cluster_manager_->removeCluster("cluster_1"));

  // Re-add the cluster. Since old params were erased, adding the cluster does not
  // bootstrap until an HTTP request arrives on the re-added cluster.
  ASSERT_TRUE(*cluster_manager_->addOrUpdateCluster(cluster_v1, "v2"));
  EXPECT_EQ(1, preconnects) << "no new bootstrap on re-added cluster before first HTTP use";

  factory_.tls_.shutdownThread();
}

// When jitter is configured, bulk bootstrap schedules a timer rather than connecting immediately.
TEST_F(EagerPreconnectFloorTest, JitterDelaysBootstrapConnection) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.25s
      type: STATIC
      lb_policy: ROUND_ROBIN
      load_assignment:
        cluster_name: cluster_1
        endpoints:
        - lb_endpoints:
          - endpoint: {address: {socket_address: {address: 127.0.0.1, port_value: 11001}}}
      preconnect_policy:
        eager_preconnect_floor:
          value: 1
        eager_preconnect_floor_jitter: 2s
  )EOF";

  auto* pool = new NiceMock<Http::ConnectionPool::MockInstance>();
  ON_CALL(*pool, hasReadyConnection()).WillByDefault(Return(false));
  ON_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _)).WillByDefault(Return(pool));

  create(parseBootstrapFromV3Yaml(yaml));
  auto* cluster = cluster_manager_->getThreadLocalCluster("cluster_1");
  ASSERT_NE(nullptr, cluster);

  Event::TimerCb timer_cb;
  EXPECT_CALL(factory_.tls_.dispatcher_, createTimer_(_))
      .WillOnce(testing::Invoke([&](Event::TimerCb cb) {
        timer_cb = cb;
        return new NiceMock<Event::MockTimer>();
      }));

  // Before timer fires, no connection is opened.
  EXPECT_CALL(*pool, maybePreconnect(_)).Times(0);
  cluster->httpConnPool(cluster->prioritySet().hostSetsPerPriority()[0]->hosts()[0],
                        ResourcePriority::Default, std::nullopt, nullptr);

  // Firing the timer callback opens the bootstrap connection.
  EXPECT_CALL(*pool, maybePreconnect(1.0f)).WillOnce(Return(true));
  ASSERT_TRUE(timer_cb != nullptr);
  timer_cb();

  factory_.tls_.shutdownThread();
}

// When jitter is set to 0s, the bootstrap connection is opened immediately without creating a
// timer.
TEST_F(EagerPreconnectFloorTest, ZeroJitterBootstrapsImmediatelyWithoutTimer) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.25s
      type: STATIC
      lb_policy: ROUND_ROBIN
      load_assignment:
        cluster_name: cluster_1
        endpoints:
        - lb_endpoints:
          - endpoint: {address: {socket_address: {address: 127.0.0.1, port_value: 11001}}}
      preconnect_policy:
        eager_preconnect_floor:
          value: 1
        eager_preconnect_floor_jitter: 0s
  )EOF";

  auto* pool = new NiceMock<Http::ConnectionPool::MockInstance>();
  ON_CALL(*pool, hasReadyConnection()).WillByDefault(Return(false));
  ON_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _)).WillByDefault(Return(pool));

  create(parseBootstrapFromV3Yaml(yaml));
  auto* cluster = cluster_manager_->getThreadLocalCluster("cluster_1");
  ASSERT_NE(nullptr, cluster);

  // With jitter 0s, no timer is created and maybePreconnect(1.0f) is called immediately.
  EXPECT_CALL(factory_.tls_.dispatcher_, createTimer_(_)).Times(0);
  EXPECT_CALL(*pool, maybePreconnect(1.0f)).WillOnce(Return(true));
  cluster->httpConnPool(cluster->prioritySet().hostSetsPerPriority()[0]->hosts()[0],
                        ResourcePriority::Default, std::nullopt, nullptr);

  factory_.tls_.shutdownThread();
}

} // namespace
} // namespace Upstream
} // namespace Envoy
