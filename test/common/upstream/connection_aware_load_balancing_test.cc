#include "test/common/upstream/cluster_manager_impl_test_common.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/mocks/upstream/priority_set.h"

namespace Envoy {
namespace Upstream {
namespace {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

constexpr char kColdHost[] = "127.0.0.1:11001";

class ConnectionAwareLbTest : public ClusterManagerImplTest {
protected:
  void TearDown() override { factory_.tls_.shutdownThread(); }

  std::string fourHostConfig(const std::string& connection_aware = "{}",
                             const std::string& preconnect = "",
                             const std::string& lb_policy = "ROUND_ROBIN") {
    std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.25s
      type: STATIC
      lb_policy: )EOF" +
                       lb_policy + "\n";
    if (!connection_aware.empty()) {
      if (lb_policy == "ROUND_ROBIN") {
        yaml += "      round_robin_lb_config:\n        connection_aware_lb_config: " +
                connection_aware + "\n";
      } else if (lb_policy == "LEAST_REQUEST") {
        yaml += "      least_request_lb_config:\n        connection_aware_lb_config: " +
                connection_aware + "\n";
      }
    }
    yaml += R"EOF(
      load_assignment:
        cluster_name: cluster_1
        endpoints:
        - lb_endpoints:
          - endpoint:
              address: { socket_address: { address: 127.0.0.1, port_value: 11001 } }
          - endpoint:
              address: { socket_address: { address: 127.0.0.1, port_value: 11002 } }
          - endpoint:
              address: { socket_address: { address: 127.0.0.1, port_value: 11003 } }
          - endpoint:
              address: { socket_address: { address: 127.0.0.1, port_value: 11004 } }
)EOF";
    if (!preconnect.empty()) {
      yaml += "      preconnect_policy: " + preconnect + "\n";
    }
    return yaml;
  }

  void stubPools(std::function<bool(const std::string&)> warm) {
    ON_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _))
        .WillByDefault([this, warm](HostConstSharedPtr host, auto&&...) {
          pooled_hosts_.insert(host->address()->asString());
          auto* pool = new NiceMock<Http::ConnectionPool::MockInstance>();
          ON_CALL(*pool, hasReadyConnection())
              .WillByDefault(Return(warm(host->address()->asString())));
          ON_CALL(*pool, maybePreconnect(_)).WillByDefault([this](float ratio) {
            preconnect_ratios_.push_back(ratio);
            return true;
          });
          return pool;
        });
  }

  void stubTcpPools(std::function<bool(const std::string&)> warm) {
    ON_CALL(factory_, allocateTcpConnPool_(_)).WillByDefault([this, warm](HostConstSharedPtr host) {
      pooled_hosts_.insert(host->address()->asString());
      auto* pool = new NiceMock<Tcp::ConnectionPool::MockInstance>();
      ON_CALL(*pool, hasReadyConnection()).WillByDefault(Return(warm(host->address()->asString())));
      ON_CALL(*pool, maybePreconnect(_)).WillByDefault(Return(true));
      return pool;
    });
  }

  // Like stubPools, but readiness is re-evaluated on each call against the live cold_hosts_ set so
  // a test can flip a host cold at runtime.
  void stubDynamicPools() {
    ON_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _))
        .WillByDefault([this](HostConstSharedPtr host, auto&&...) {
          const std::string addr = host->address()->asString();
          pooled_hosts_.insert(addr);
          auto* pool = new NiceMock<Http::ConnectionPool::MockInstance>();
          ON_CALL(*pool, hasReadyConnection()).WillByDefault([this, addr] {
            return !cold_hosts_.contains(addr);
          });
          ON_CALL(*pool, maybePreconnect(_)).WillByDefault(Return(true));
          return pool;
        });
  }

  ThreadLocalCluster& cluster() {
    auto* cluster = cluster_manager_->getThreadLocalCluster("cluster_1");
    EXPECT_NE(nullptr, cluster);
    return *cluster;
  }

  // Allocate a pool for every host except `cold`.
  void prewarmExcept(const std::string& cold) {
    for (const auto& host_set : cluster().prioritySet().hostSetsPerPriority()) {
      for (const auto& host : host_set->hosts()) {
        if (host->address()->asString() == cold) {
          continue;
        }
        ASSERT_TRUE(
            cluster()
                .httpConnPool(host, ResourcePriority::Default, Http::Protocol::Http11, nullptr)
                .has_value());
      }
    }
  }

  // Allocate a TCP pool for every host except `cold`.
  void prewarmTcpExcept(const std::string& cold) {
    for (const auto& host_set : cluster().prioritySet().hostSetsPerPriority()) {
      for (const auto& host : host_set->hosts()) {
        if (host->address()->asString() == cold) {
          continue;
        }
        ASSERT_TRUE(cluster().tcpConnPool(host, ResourcePriority::Default, nullptr).has_value());
      }
    }
  }

  uint64_t skippedCold() {
    return cluster().info()->lbStats().lb_connection_aware_skipped_cold_.value();
  }
  uint64_t selectedCold() {
    return cluster().info()->lbStats().lb_connection_aware_selected_cold_.value();
  }

  absl::flat_hash_set<std::string> pooled_hosts_;
  absl::flat_hash_set<std::string> cold_hosts_;
  std::vector<float> preconnect_ratios_;
};

// -----------------------------------------------------------------------------
// Config surface
// -----------------------------------------------------------------------------

TEST_F(ConnectionAwareLbTest, Disabled) {
  stubPools([](const std::string& addr) { return addr != kColdHost; });
  create(parseBootstrapFromV3Yaml(fourHostConfig(/*connection_aware=*/"")));
  prewarmExcept(kColdHost);

  bool served_cold = false;
  for (int i = 0; i < 4; ++i) {
    auto host = cluster().chooseHost(nullptr).host;
    ASSERT_NE(nullptr, host);
    served_cold |= (host->address()->asString() == kColdHost);
  }
  EXPECT_TRUE(served_cold) << "without connection_aware_lb_config, cold host is served on its turn";
  EXPECT_EQ(0U, skippedCold());
  EXPECT_EQ(0U, selectedCold());
}

TEST_F(ConnectionAwareLbTest, Enabled) {
  stubPools([](const std::string& addr) { return addr != kColdHost; });
  create(parseBootstrapFromV3Yaml(fourHostConfig("{}")));
  prewarmExcept(kColdHost);

  for (int i = 0; i < 4; ++i) {
    auto host = cluster().chooseHost(nullptr).host;
    ASSERT_NE(nullptr, host);
    EXPECT_NE(kColdHost, host->address()->asString());
  }
  EXPECT_GT(skippedCold(), 0U);
  EXPECT_EQ(0U, selectedCold());
}

TEST_F(ConnectionAwareLbTest, RetryAttemptsConfigurable) {
  stubPools([](const std::string& addr) { return addr != kColdHost; });
  create(parseBootstrapFromV3Yaml(fourHostConfig("{ host_selection_retry_max_attempts: 0 }")));
  prewarmExcept(kColdHost);

  bool served_cold = false;
  for (int i = 0; i < 4; ++i) {
    auto host = cluster().chooseHost(nullptr).host;
    ASSERT_NE(nullptr, host);
    served_cold |= (host->address()->asString() == kColdHost);
  }
  EXPECT_TRUE(served_cold) << "with 0 retry attempts, cold host is picked when selected";
  EXPECT_GT(selectedCold(), 0U);
}

// -----------------------------------------------------------------------------
// Selection behavior
// -----------------------------------------------------------------------------

TEST_F(ConnectionAwareLbTest, AllHostsWarm) {
  stubPools([](const std::string&) { return true; });
  create(parseBootstrapFromV3Yaml(fourHostConfig()));
  prewarmExcept(/*cold=*/"");

  absl::flat_hash_set<std::string> picked;
  for (int i = 0; i < 4; ++i) {
    auto host = cluster().chooseHost(nullptr).host;
    ASSERT_NE(nullptr, host);
    picked.insert(host->address()->asString());
  }

  EXPECT_EQ(4U, picked.size()) << "warm round-robin should visit all four hosts";
  EXPECT_EQ(0U, skippedCold());
  EXPECT_EQ(0U, selectedCold());
}

TEST_F(ConnectionAwareLbTest, AllHostsCold) {
  stubPools([](const std::string&) { return false; });
  create(parseBootstrapFromV3Yaml(fourHostConfig()));

  for (int i = 0; i < 4; ++i) {
    EXPECT_NE(nullptr, cluster().chooseHost(nullptr).host) << "cold start must fall back to a host";
  }

  EXPECT_EQ(12U, skippedCold()) << "4 picks, each attempting 3 total selections on cold hosts";
  EXPECT_EQ(4U, selectedCold());
}

TEST_F(ConnectionAwareLbTest, PrefersWarmOverCold) {
  stubPools([](const std::string& addr) { return addr != kColdHost; });
  create(parseBootstrapFromV3Yaml(fourHostConfig()));
  prewarmExcept(kColdHost);

  constexpr int kRequests = 8;
  for (int i = 0; i < kRequests; ++i) {
    auto host = cluster().chooseHost(nullptr).host;
    ASSERT_NE(nullptr, host);
    EXPECT_NE(kColdHost, host->address()->asString())
        << "cold host must not be served while warm peers exist";
  }

  EXPECT_GT(skippedCold(), 0U);
  EXPECT_EQ(0U, selectedCold());
  EXPECT_EQ(1U, pooled_hosts_.count(kColdHost))
      << "a rejected cold host is primed in the background";
}

TEST_F(ConnectionAwareLbTest, PrefersWarmOverColdWithFloor) {
  stubPools([](const std::string& addr) { return addr != kColdHost; });
  create(parseBootstrapFromV3Yaml(
      fourHostConfig(/*connection_aware=*/"{}", /*preconnect=*/"{ eager_preconnect_floor: 2 }")));
  prewarmExcept(kColdHost);

  constexpr int kRequests = 8;
  for (int i = 0; i < kRequests; ++i) {
    auto host = cluster().chooseHost(nullptr).host;
    ASSERT_NE(nullptr, host);
    EXPECT_NE(kColdHost, host->address()->asString())
        << "cold host must not be served while warm peers exist";
  }

  EXPECT_GT(skippedCold(), 0U);
  EXPECT_EQ(0U, selectedCold());
  EXPECT_EQ(1U, pooled_hosts_.count(kColdHost))
      << "with the eager preconnect floor a rejected cold host is primed";
}

TEST_F(ConnectionAwareLbTest, ColdHostPrimedWithoutFloor) {
  stubPools([](const std::string& addr) { return addr != kColdHost; });
  create(parseBootstrapFromV3Yaml(fourHostConfig()));
  prewarmExcept(kColdHost);

  cluster().chooseHost(nullptr);
  ASSERT_FALSE(preconnect_ratios_.empty());
  EXPECT_FLOAT_EQ(1.0f, preconnect_ratios_.back());
}

TEST_F(ConnectionAwareLbTest, ColdHostPrimedWithFloor) {
  stubPools([](const std::string& addr) { return addr != kColdHost; });
  create(parseBootstrapFromV3Yaml(
      fourHostConfig(/*connection_aware=*/"{}", /*preconnect=*/"{ eager_preconnect_floor: 2 }")));
  prewarmExcept(kColdHost);

  cluster().chooseHost(nullptr);
  ASSERT_FALSE(preconnect_ratios_.empty());
  EXPECT_FLOAT_EQ(1.0f, preconnect_ratios_.back());
}

TEST_F(ConnectionAwareLbTest, RetryBudgetZeroDisablesRepick) {
  stubPools([](const std::string& addr) { return addr != kColdHost; });
  create(parseBootstrapFromV3Yaml(fourHostConfig("{ host_selection_retry_max_attempts: 0 }")));
  prewarmExcept(kColdHost);

  bool served_cold = false;
  constexpr int kRequests = 8;
  for (int i = 0; i < kRequests; ++i) {
    auto host = cluster().chooseHost(nullptr).host;
    ASSERT_NE(nullptr, host);
    served_cold |= host->address()->asString() == kColdHost;
  }

  EXPECT_TRUE(served_cold) << "with no retries the cold host is served on its round-robin turn";
  EXPECT_GT(selectedCold(), 0U);
}

// TCP connections do not make a host warm for HTTP selection; CALB is scoped to HTTP.
TEST_F(ConnectionAwareLbTest, TcpConnectionsDoNotMakeHostWarmForHttp) {
  stubPools([](const std::string& addr) { return addr != kColdHost; });
  stubTcpPools([](const std::string& addr) { return addr == kColdHost; });
  create(parseBootstrapFromV3Yaml(fourHostConfig()));
  prewarmExcept(kColdHost);
  prewarmTcpExcept(/*cold=*/"");

  constexpr int kRequests = 8;
  for (int i = 0; i < kRequests; ++i) {
    auto host = cluster().chooseHost(nullptr).host;
    ASSERT_NE(nullptr, host);
    EXPECT_NE(kColdHost, host->address()->asString())
        << "cold HTTP host must not be served even if it has ready TCP connections";
  }

  EXPECT_GT(skippedCold(), 0U);
  EXPECT_EQ(0U, selectedCold());
}

// When hosts only have TCP connections, CALB treats all hosts as cold.
TEST_F(ConnectionAwareLbTest, TcpOnlyConnectionsTreatedAsCold) {
  stubPools([](const std::string&) { return false; });
  stubTcpPools([](const std::string&) { return true; });
  create(parseBootstrapFromV3Yaml(fourHostConfig()));
  prewarmTcpExcept(/*cold=*/"");

  for (int i = 0; i < 4; ++i) {
    EXPECT_NE(nullptr, cluster().chooseHost(nullptr).host);
  }

  EXPECT_EQ(12U, skippedCold());
  EXPECT_EQ(4U, selectedCold());
}

TEST_F(ConnectionAwareLbTest, LeastRequestPrefersWarmOverCold) {
  ON_CALL(factory_.server_context_.api_.random_, random()).WillByDefault([i = 0]() mutable {
    return i++;
  });
  stubPools([](const std::string& addr) { return addr != kColdHost; });
  create(parseBootstrapFromV3Yaml(
      fourHostConfig(/*connection_aware=*/"{}", /*preconnect=*/"", /*lb_policy=*/"LEAST_REQUEST")));
  prewarmExcept(kColdHost);

  constexpr int kRequests = 8;
  for (int i = 0; i < kRequests; ++i) {
    auto host = cluster().chooseHost(nullptr).host;
    ASSERT_NE(nullptr, host);
    EXPECT_NE(kColdHost, host->address()->asString())
        << "cold host must not be served by least request while warm peers exist";
  }

  EXPECT_GT(skippedCold(), 0U);
  EXPECT_EQ(0U, selectedCold());
}

TEST_F(ConnectionAwareLbTest, TypedPolicyConfig) {
  stubPools([](const std::string& addr) { return addr != kColdHost; });
  const std::string yaml = R"EOF(
static_resources:
  clusters:
  - name: cluster_1
    connect_timeout: 0.25s
    type: STATIC
    load_balancing_policy:
      policies:
      - typed_extension_config:
          name: envoy.load_balancing_policies.round_robin
          typed_config:
            "@type": >-
              type.googleapis.com/envoy.extensions.load_balancing_policies.round_robin.v3.RoundRobin
            connection_aware_lb_config:
              host_selection_retry_max_attempts: 2
    load_assignment:
      cluster_name: cluster_1
      endpoints:
      - lb_endpoints:
        - endpoint:
            address: { socket_address: { address: 127.0.0.1, port_value: 11001 } }
        - endpoint:
            address: { socket_address: { address: 127.0.0.1, port_value: 11002 } }
        - endpoint:
            address: { socket_address: { address: 127.0.0.1, port_value: 11003 } }
        - endpoint:
            address: { socket_address: { address: 127.0.0.1, port_value: 11004 } }
  )EOF";
  create(parseBootstrapFromV3Yaml(yaml));
  prewarmExcept(kColdHost);

  constexpr int kRequests = 8;
  for (int i = 0; i < kRequests; ++i) {
    auto host = cluster().chooseHost(nullptr).host;
    ASSERT_NE(nullptr, host);
    EXPECT_NE(kColdHost, host->address()->asString());
  }

  EXPECT_GT(skippedCold(), 0U);
  EXPECT_EQ(0U, selectedCold());
}

TEST_F(ConnectionAwareLbTest, TypedLeastRequestPolicyConfig) {
  ON_CALL(factory_.server_context_.api_.random_, random()).WillByDefault([i = 0]() mutable {
    return i++;
  });
  stubPools([](const std::string& addr) { return addr != kColdHost; });
  const std::string yaml = R"EOF(
static_resources:
  clusters:
  - name: cluster_1
    connect_timeout: 0.25s
    type: STATIC
    load_balancing_policy:
      policies:
      - typed_extension_config:
          name: envoy.load_balancing_policies.least_request
          typed_config:
            "@type": >-
              type.googleapis.com/envoy.extensions.load_balancing_policies.least_request.v3.LeastRequest
            connection_aware_lb_config:
              host_selection_retry_max_attempts: 2
    load_assignment:
      cluster_name: cluster_1
      endpoints:
      - lb_endpoints:
        - endpoint:
            address: { socket_address: { address: 127.0.0.1, port_value: 11001 } }
        - endpoint:
            address: { socket_address: { address: 127.0.0.1, port_value: 11002 } }
        - endpoint:
            address: { socket_address: { address: 127.0.0.1, port_value: 11003 } }
        - endpoint:
            address: { socket_address: { address: 127.0.0.1, port_value: 11004 } }
  )EOF";
  create(parseBootstrapFromV3Yaml(yaml));
  prewarmExcept(kColdHost);

  constexpr int kRequests = 8;
  for (int i = 0; i < kRequests; ++i) {
    auto host = cluster().chooseHost(nullptr).host;
    ASSERT_NE(nullptr, host);
    EXPECT_NE(kColdHost, host->address()->asString());
  }

  EXPECT_GT(skippedCold(), 0U);
  EXPECT_EQ(0U, selectedCold());
}

TEST_F(ConnectionAwareLbTest, ContextFilterWithConnectionAwareLb) {
  stubPools([](const std::string& addr) { return addr != kColdHost; });
  create(parseBootstrapFromV3Yaml(fourHostConfig()));
  prewarmExcept(kColdHost);

  NiceMock<MockLoadBalancerContext> context;
  ON_CALL(context, hostSelectionRetryCount()).WillByDefault(Return(2));
  ON_CALL(context, shouldSelectAnotherHost(_)).WillByDefault([](const Host& host) {
    return host.address()->asString() == "127.0.0.1:11002";
  });

  for (int i = 0; i < 4; ++i) {
    auto host = cluster().chooseHost(&context).host;
    ASSERT_NE(nullptr, host);
    EXPECT_NE(kColdHost, host->address()->asString());
    EXPECT_NE("127.0.0.1:11002", host->address()->asString());
  }
}

TEST_F(ConnectionAwareLbTest, ContextRetriesDoNotExhaustConnectionAwareBudget) {
  // All hosts are cold.
  stubPools([](const std::string&) { return false; });
  create(parseBootstrapFromV3Yaml(fourHostConfig()));

  NiceMock<MockLoadBalancerContext> context;
  ON_CALL(context, hostSelectionRetryCount()).WillByDefault(Return(2));
  // Context rejects host 11002.
  ON_CALL(context, shouldSelectAnotherHost(_)).WillByDefault([](const Host& host) {
    return host.address()->asString() == "127.0.0.1:11002";
  });

  auto host = cluster().chooseHost(&context).host;
  ASSERT_NE(nullptr, host);
  EXPECT_NE("127.0.0.1:11002", host->address()->asString());
  EXPECT_EQ(1U, selectedCold());
  EXPECT_EQ(3U, skippedCold());
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
      round_robin_lb_config:
        connection_aware_lb_config: {}
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

  EXPECT_THROW_WITH_MESSAGE(
      create(parseBootstrapFromV3Yaml(yaml)), EnvoyException,
      "connection_aware_lb_config is incompatible with connection_pool_per_downstream_connection");
}

// The LB may pick no host (e.g. when none exist), returning null.
TEST_F(ConnectionAwareLbTest, NoHostReturnsNull) {
  stubPools([](const std::string&) { return false; });
  create(parseBootstrapFromV3Yaml(fourHostConfig()));

  Cluster& active = cluster_manager_->activeClusters().begin()->second;
  HostVector removed;
  for (const auto& host_set : active.prioritySet().hostSetsPerPriority()) {
    for (const auto& host : host_set->hosts()) {
      removed.push_back(host);
    }
  }
  active.prioritySet().updateHosts(
      0, HostSetImpl::partitionHosts(std::make_shared<HostVector>(), HostsPerLocalityImpl::empty()),
      nullptr, {}, removed, std::nullopt, 100);

  EXPECT_EQ(nullptr, cluster().chooseHost(nullptr).host);
}

} // namespace
} // namespace Upstream
} // namespace Envoy
