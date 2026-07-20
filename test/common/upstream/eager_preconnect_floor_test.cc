// Tests for the eager_preconnect_floor preconnect feature.

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

  EXPECT_THROW_WITH_MESSAGE(create(parseBootstrapFromV3Yaml(yaml)), EnvoyException,
                            "eager_preconnect_floor and connection_aware_load_balancing are "
                            "incompatible with connection_pool_per_downstream_connection");
}

TEST_F(ClusterManagerImplTest, ConnectionAwareLbIncompatibleWithPoolPerDownstreamConnection) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.25s
      type: STATIC
      lb_policy: ROUND_ROBIN
      connection_pool_per_downstream_connection: true
      connection_aware_load_balancing: {}
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

  EXPECT_THROW_WITH_MESSAGE(create(parseBootstrapFromV3Yaml(yaml)), EnvoyException,
                            "eager_preconnect_floor and connection_aware_load_balancing are "
                            "incompatible with connection_pool_per_downstream_connection");
}

TEST_F(EagerPreconnectFloorTest, RefillsAfterPoolErased) {
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

  // Preconnect floor is established only after the cluster is used as HTTP, so nothing is allocated
  // at init.
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

  // Another bootstrap connection should re-open the pool.
  EXPECT_EQ(2, pools_allocated) << "cluster manager should re-fill after pool erase";

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
  EXPECT_CALL(*pool, maybePreconnect(0)).WillOnce(Return(true));
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

  // Preconnect floor is configured, but the guard is off, so no bootstrap connection is opened.
  EXPECT_CALL(*pool, maybePreconnect(_)).Times(0);
  cluster->httpConnPool(hosts[0], ResourcePriority::Default, std::nullopt, nullptr);

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

  // Preconnect floor is not configured, even though the guard is on, so no bootstrap connection is
  // opened.
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
  EXPECT_CALL(*pool, maybePreconnect(0)).WillOnce(Return(true));
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
        ON_CALL(*pool, maybePreconnect(0)).WillByDefault([&preconnects] {
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
  EXPECT_CALL(*pool_, maybePreconnect(0)).WillOnce(Return(true));
  posted_[0]();
  factory_.tls_.shutdownThread();
}

TEST_F(BootstrapEligibilityRecheckTest, BootstrapsHostWithFailuresBelowThreshold) {
  armAndCaptureBootstrap(/*failure_threshold=*/2);
  host_->incConsecutiveEagerPreconnectFloorFailures();
  EXPECT_CALL(*pool_, maybePreconnect(0)).WillOnce(Return(true));
  posted_[0]();
  factory_.tls_.shutdownThread();
}

// A TCP-proxied cluster maintains its preconnect floor through the TCP pool.
TEST_F(EagerPreconnectFloorTest, TcpFloorFillDrivesTcpPool) {
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

  auto* tcp_pool = new NiceMock<Tcp::ConnectionPool::MockInstance>();
  ON_CALL(factory_, allocateTcpConnPool_(_)).WillByDefault(Return(tcp_pool));

  create(parseBootstrapFromV3Yaml(yaml));
  auto* cluster = cluster_manager_->getThreadLocalCluster("cluster_1");
  ASSERT_NE(nullptr, cluster);
  const auto& hosts = cluster->prioritySet().hostSetsPerPriority()[0]->hosts();
  ASSERT_EQ(1, hosts.size());

  // First TCP use marks the cluster as used as TCP and opens a bootstrap connection.
  EXPECT_CALL(*tcp_pool, maybePreconnect(0)).WillOnce(Return(true));
  cluster->tcpConnPool(hosts[0], ResourcePriority::Default, nullptr);

  factory_.tls_.shutdownThread();
}

// If the host's TCP pool already reports a ready connection, no bootstrap connection is opened.
TEST_F(EagerPreconnectFloorTest, TcpFloorFillSkippedWhenTcpPoolReady) {
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

  auto* tcp_pool = new NiceMock<Tcp::ConnectionPool::MockInstance>();
  ON_CALL(*tcp_pool, hasReadyConnection()).WillByDefault(Return(true));
  ON_CALL(factory_, allocateTcpConnPool_(_)).WillByDefault(Return(tcp_pool));

  create(parseBootstrapFromV3Yaml(yaml));
  auto* cluster = cluster_manager_->getThreadLocalCluster("cluster_1");
  ASSERT_NE(nullptr, cluster);
  const auto& hosts = cluster->prioritySet().hostSetsPerPriority()[0]->hosts();
  ASSERT_EQ(1, hosts.size());

  // The pool the request creates already reports a ready connection, so no bootstrap connection is
  // opened.
  EXPECT_CALL(*tcp_pool, maybePreconnect(_)).Times(0);
  cluster->tcpConnPool(hosts[0], ResourcePriority::Default, nullptr);

  factory_.tls_.shutdownThread();
}

// A host with no TCP conn-pool container does not get a bootstrap connection.
TEST_F(EagerPreconnectFloorTest, FloorReadinessHandlesHostWithoutTcpContainer) {
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

  ON_CALL(factory_, allocateTcpConnPool_(_))
      .WillByDefault(ReturnNew<NiceMock<Tcp::ConnectionPool::MockInstance>>());

  create(parseBootstrapFromV3Yaml(yaml));
  auto* cluster = cluster_manager_->getThreadLocalCluster("cluster_1");
  ASSERT_NE(nullptr, cluster);
  const auto& hosts = cluster->prioritySet().hostSetsPerPriority()[0]->hosts();
  ASSERT_EQ(2, hosts.size());

  // Use only host[0] via TCP. Host[1] has no TCP container, so no bootstrap connection is opened.
  cluster->tcpConnPool(hosts[0], ResourcePriority::Default, nullptr);

  factory_.tls_.shutdownThread();
}

TEST_F(EagerPreconnectFloorTest, TcpRefillsAfterPoolErased) {
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

  // Count TCP pool allocations.
  Tcp::ConnectionPool::Instance::IdleCb captured_idle_cb;
  uint32_t pools_allocated = 0;
  ON_CALL(factory_, allocateTcpConnPool_(_))
      .WillByDefault([&captured_idle_cb, &pools_allocated](auto&&...) {
        ++pools_allocated;
        auto* pool = new NiceMock<Tcp::ConnectionPool::MockInstance>();
        ON_CALL(*pool, addIdleCallback(_))
            .WillByDefault([&captured_idle_cb](Tcp::ConnectionPool::Instance::IdleCb cb) {
              captured_idle_cb = std::move(cb);
            });
        return pool;
      });

  create(parseBootstrapFromV3Yaml(yaml));

  // Preconnect floor is established only after the cluster is used as TCP, so nothing is allocated
  // at init.
  ASSERT_EQ(0, pools_allocated) << "no floor maintenance before the cluster is used as TCP";

  // First TCP use creates a pool and opens a bootstrap connection.
  auto* cluster = cluster_manager_->getThreadLocalCluster("cluster_1");
  ASSERT_NE(nullptr, cluster);
  const auto& hosts = cluster->prioritySet().hostSetsPerPriority()[0]->hosts();
  ASSERT_EQ(1, hosts.size());
  auto opt_pool = cluster->tcpConnPool(hosts[0], ResourcePriority::Default, nullptr);
  ASSERT_TRUE(opt_pool.has_value());
  ASSERT_EQ(1, pools_allocated);
  ASSERT_TRUE(static_cast<bool>(captured_idle_cb))
      << "cluster manager should have registered an idle callback on the filled TCP pool";

  // Make the pool fully idle.
  captured_idle_cb();

  // Another bootstrap connection should re-open the pool.
  EXPECT_EQ(2, pools_allocated) << "cluster manager should re-fill the TCP floor after pool erase";

  factory_.tls_.shutdownThread();
}

// A cluster used as both HTTP and TCP opens a bootstrap connection for each pool type used.
TEST_F(EagerPreconnectFloorTest, BootstrapsHttpAndTcpPools) {
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
  auto* tcp_pool = new NiceMock<Tcp::ConnectionPool::MockInstance>();
  ON_CALL(*tcp_pool, hasReadyConnection()).WillByDefault(Return(false));
  ON_CALL(factory_, allocateTcpConnPool_(_)).WillByDefault(Return(tcp_pool));

  create(parseBootstrapFromV3Yaml(yaml));
  auto* cluster = cluster_manager_->getThreadLocalCluster("cluster_1");
  ASSERT_NE(nullptr, cluster);
  const auto& hosts = cluster->prioritySet().hostSetsPerPriority()[0]->hosts();
  ASSERT_EQ(1, hosts.size());

  // Neither pool ever reports a ready connection, so both types get a bootstrap connection.
  EXPECT_CALL(*http_pool, maybePreconnect(0)).Times(::testing::AtLeast(1));
  EXPECT_CALL(*tcp_pool, maybePreconnect(0)).Times(::testing::AtLeast(1));
  cluster->httpConnPool(hosts[0], ResourcePriority::Default, std::nullopt, nullptr);
  cluster->tcpConnPool(hosts[0], ResourcePriority::Default, nullptr);

  factory_.tls_.shutdownThread();
}

} // namespace
} // namespace Upstream
} // namespace Envoy
