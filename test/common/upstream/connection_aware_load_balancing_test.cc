#include "source/common/upstream/connection_aware_lb_context.h"

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
                       lb_policy + R"EOF(
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
    if (!connection_aware.empty()) {
      yaml += "      connection_aware_load_balancing: " + connection_aware + "\n";
    }
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
          ON_CALL(*pool, maybePreconnect(_)).WillByDefault(Return(true));
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

  uint64_t warmSelected() {
    return cluster().info()->trafficStats()->upstream_cx_lb_selected_warm_.value();
  }
  uint64_t coldSelected() {
    return cluster().info()->trafficStats()->upstream_cx_lb_selected_cold_.value();
  }

  absl::flat_hash_set<std::string> pooled_hosts_;
  absl::flat_hash_set<std::string> cold_hosts_;
};

// -----------------------------------------------------------------------------
// Config surface
// -----------------------------------------------------------------------------

TEST_F(ConnectionAwareLbTest, Disabled) {
  create(parseBootstrapFromV3Yaml(fourHostConfig(/*connection_aware=*/"")));
  EXPECT_FALSE(cluster().info()->connectionAwareLoadBalancingEnabled());
}

TEST_F(ConnectionAwareLbTest, Enabled) {
  create(parseBootstrapFromV3Yaml(fourHostConfig("{}")));
  EXPECT_TRUE(cluster().info()->connectionAwareLoadBalancingEnabled());
  EXPECT_EQ(2U, cluster().info()->connectionAwareLbHostSelectionRetryMaxAttempts());
}

TEST_F(ConnectionAwareLbTest, RetryAttemptsConfigurable) {
  create(parseBootstrapFromV3Yaml(fourHostConfig("{ host_selection_retry_max_attempts: 5 }")));
  EXPECT_TRUE(cluster().info()->connectionAwareLoadBalancingEnabled());
  EXPECT_EQ(5U, cluster().info()->connectionAwareLbHostSelectionRetryMaxAttempts());
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
  EXPECT_EQ(4U, warmSelected());
  EXPECT_EQ(0U, coldSelected());
}

TEST_F(ConnectionAwareLbTest, AllHostsCold) {
  stubPools([](const std::string&) { return false; });
  create(parseBootstrapFromV3Yaml(fourHostConfig()));

  for (int i = 0; i < 4; ++i) {
    EXPECT_NE(nullptr, cluster().chooseHost(nullptr).host) << "cold start must fall back to a host";
  }

  EXPECT_EQ(0U, warmSelected());
  EXPECT_EQ(4U, coldSelected());
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

  EXPECT_EQ(kRequests, warmSelected());
  EXPECT_EQ(0U, coldSelected());
  EXPECT_EQ(0U, pooled_hosts_.count(kColdHost))
      << "without the eager preconnect floor a rejected cold host is not primed";
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

  EXPECT_EQ(kRequests, warmSelected());
  EXPECT_EQ(0U, coldSelected());
  EXPECT_EQ(1U, pooled_hosts_.count(kColdHost))
      << "with the eager preconnect floor a rejected cold host is primed";
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
  EXPECT_GT(coldSelected(), 0U);
  EXPECT_EQ(kRequests, warmSelected() + coldSelected());
}

TEST_F(ConnectionAwareLbTest, AllHostsWarmTcp) {
  stubTcpPools([](const std::string&) { return true; });
  create(parseBootstrapFromV3Yaml(fourHostConfig()));
  prewarmTcpExcept(/*cold=*/"");

  absl::flat_hash_set<std::string> picked;
  for (int i = 0; i < 4; ++i) {
    auto host = cluster().chooseHost(nullptr).host;
    ASSERT_NE(nullptr, host);
    picked.insert(host->address()->asString());
  }

  EXPECT_EQ(4U, picked.size()) << "warm round-robin should visit all four hosts";
  EXPECT_EQ(4U, warmSelected());
  EXPECT_EQ(0U, coldSelected());
}

TEST_F(ConnectionAwareLbTest, PrefersWarmOverColdTcp) {
  stubTcpPools([](const std::string& addr) { return addr != kColdHost; });
  create(parseBootstrapFromV3Yaml(fourHostConfig()));
  prewarmTcpExcept(kColdHost);

  constexpr int kRequests = 8;
  for (int i = 0; i < kRequests; ++i) {
    auto host = cluster().chooseHost(nullptr).host;
    ASSERT_NE(nullptr, host);
    EXPECT_NE(kColdHost, host->address()->asString())
        << "cold host must not be served while warm peers exist";
  }

  EXPECT_EQ(kRequests, warmSelected());
  EXPECT_EQ(0U, coldSelected());
}

// A host is warm if any used pool type has a ready connection.
TEST_F(ConnectionAwareLbTest, WarmIfAnyPoolTypeReady) {
  stubPools([](const std::string&) { return false; });
  stubTcpPools([](const std::string&) { return true; });
  create(parseBootstrapFromV3Yaml(fourHostConfig()));
  prewarmExcept(/*cold=*/"");
  prewarmTcpExcept(/*cold=*/"");

  absl::flat_hash_set<std::string> picked;
  for (int i = 0; i < 4; ++i) {
    auto host = cluster().chooseHost(nullptr).host;
    ASSERT_NE(nullptr, host);
    picked.insert(host->address()->asString());
  }

  EXPECT_EQ(4U, picked.size());
  EXPECT_EQ(4U, warmSelected()) << "a ready TCP pool makes the host warm despite a cold HTTP pool";
  EXPECT_EQ(0U, coldSelected());
}

// With a consistent-hash LB, selection re-picks around the ring.
TEST_F(ConnectionAwareLbTest, HashLbRepicksColdHashTarget) {
  stubDynamicPools();
  create(parseBootstrapFromV3Yaml(
      fourHostConfig(/*connection_aware=*/"{}", /*preconnect=*/"", /*lb_policy=*/"RING_HASH")));
  prewarmExcept(/*cold=*/"");

  NiceMock<MockLoadBalancerContext> context;
  ON_CALL(context, computeHashKey()).WillByDefault(Return(std::optional<uint64_t>(0x1234)));

  auto hashed = cluster().chooseHost(&context).host;
  ASSERT_NE(nullptr, hashed);
  const std::string hashed_addr = hashed->address()->asString();
  EXPECT_EQ(hashed_addr, cluster().chooseHost(&context).host->address()->asString())
      << "same hash key must map to the same warm host";

  // The hashed host goes cold and we re-pick.
  cold_hosts_.insert(hashed_addr);
  auto repicked = cluster().chooseHost(&context).host;
  ASSERT_NE(nullptr, repicked);
  EXPECT_NE(hashed_addr, repicked->address()->asString())
      << "cold hash target should be re-picked around the ring";
  EXPECT_EQ(0U, cold_hosts_.count(repicked->address()->asString()))
      << "re-picked host should be warm";
}

// The LB may pick no host (e.g. when none exist), making the wrapper pass through.
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

// -----------------------------------------------------------------------------
// ConnectionAwareLbContext delegation.
// -----------------------------------------------------------------------------

TEST(ConnectionAwareLbContextTest, ForwardsToWrappedContext) {
  NiceMock<MockLoadBalancerContext> wrapped;
  NiceMock<MockHost> host;
  ConnectionAwareLbContext ctx(
      &wrapped, [](const Host&) { return false; }, [](const Host&) {}, /*retry_budget=*/3);

  ON_CALL(wrapped, computeHashKey()).WillByDefault(Return(std::optional<uint64_t>(42)));
  EXPECT_EQ(42U, ctx.computeHashKey());

  ON_CALL(wrapped, hostSelectionRetryCount()).WillByDefault(Return(5));
  EXPECT_EQ(5U, ctx.hostSelectionRetryCount()) << "max(wrapped, budget)";

  // Each remaining accessor delegates exactly once and returns the wrapped context's value.
  EXPECT_CALL(wrapped, metadataMatchCriteria());
  EXPECT_CALL(wrapped, downstreamConnection());
  EXPECT_CALL(wrapped, requestStreamInfo());
  EXPECT_CALL(wrapped, downstreamHeaders());
  EXPECT_CALL(wrapped, upstreamSocketOptions());
  EXPECT_CALL(wrapped, upstreamTransportSocketOptions());
  EXPECT_CALL(wrapped, overrideHostToSelect());
  EXPECT_CALL(wrapped, onAsyncHostSelection(_, _));
  EXPECT_CALL(wrapped, setHeadersModifier(_));

  EXPECT_EQ(nullptr, ctx.metadataMatchCriteria());
  EXPECT_EQ(nullptr, ctx.downstreamConnection());
  EXPECT_EQ(nullptr, ctx.requestStreamInfo());
  EXPECT_EQ(nullptr, ctx.downstreamHeaders());
  EXPECT_EQ(nullptr, ctx.upstreamSocketOptions());
  EXPECT_EQ(nullptr, ctx.upstreamTransportSocketOptions());
  EXPECT_FALSE(ctx.overrideHostToSelect().has_value());
  ctx.onAsyncHostSelection(nullptr, "details");
  ctx.setHeadersModifier([](Http::ResponseHeaderMap&) {});

  NiceMock<MockPrioritySet> priority_set;
  HealthyAndDegradedLoad load;
  Upstream::RetryPriority::PriorityMappingFunc mapping;
  EXPECT_CALL(wrapped, determinePriorityLoad(_, _, _));
  ctx.determinePriorityLoad(priority_set, load, mapping);

  ON_CALL(wrapped, shouldSelectAnotherHost(_)).WillByDefault(Return(true));
  EXPECT_TRUE(ctx.shouldSelectAnotherHost(host));
}

TEST(ConnectionAwareLbContextTest, DefaultsWhenNoWrappedContext) {
  NiceMock<MockHost> host;
  bool primed = false;
  bool needs_priming = false;
  ConnectionAwareLbContext ctx(
      /*wrapped=*/nullptr, [&](const Host&) { return needs_priming; },
      [&](const Host&) { primed = true; }, /*retry_budget=*/4);

  EXPECT_FALSE(ctx.computeHashKey().has_value());
  EXPECT_EQ(nullptr, ctx.metadataMatchCriteria());
  EXPECT_EQ(nullptr, ctx.downstreamConnection());
  EXPECT_EQ(nullptr, ctx.requestStreamInfo());
  EXPECT_EQ(nullptr, ctx.downstreamHeaders());
  EXPECT_EQ(nullptr, ctx.upstreamSocketOptions());
  EXPECT_EQ(nullptr, ctx.upstreamTransportSocketOptions());
  EXPECT_FALSE(ctx.overrideHostToSelect().has_value());
  EXPECT_EQ(4U, ctx.hostSelectionRetryCount());
  ctx.onAsyncHostSelection(nullptr, "details");
  ctx.setHeadersModifier([](Http::ResponseHeaderMap&) {});

  NiceMock<MockPrioritySet> priority_set;
  HealthyAndDegradedLoad load;
  Upstream::RetryPriority::PriorityMappingFunc mapping;
  EXPECT_EQ(&load, &ctx.determinePriorityLoad(priority_set, load, mapping))
      << "returns the original load when there is no wrapped context";

  EXPECT_FALSE(ctx.shouldSelectAnotherHost(host));
  EXPECT_FALSE(primed);
  needs_priming = true;
  EXPECT_TRUE(ctx.shouldSelectAnotherHost(host));
  EXPECT_TRUE(primed) << "a rejected host is primed";
}

} // namespace
} // namespace Upstream
} // namespace Envoy
