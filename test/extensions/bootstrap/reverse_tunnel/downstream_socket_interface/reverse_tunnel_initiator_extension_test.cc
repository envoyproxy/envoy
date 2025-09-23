#include <memory>

#include "envoy/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/v3/downstream_reverse_connection_socket_interface.pb.h"
#include "envoy/server/factory_context.h"
#include "envoy/thread_local/thread_local.h"

#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_tunnel_initiator.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_tunnel_initiator_extension.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class ReverseTunnelInitiatorExtensionTest : public testing::Test {
protected:
  ReverseTunnelInitiatorExtensionTest() {
    // Set up the stats scope.
    stats_scope_ = Stats::ScopeSharedPtr(stats_store_.createScope("test_scope."));

    // Set up the mock context.
    EXPECT_CALL(context_, threadLocal()).WillRepeatedly(ReturnRef(thread_local_));
    EXPECT_CALL(context_, scope()).WillRepeatedly(ReturnRef(*stats_scope_));
    EXPECT_CALL(context_, clusterManager()).WillRepeatedly(ReturnRef(cluster_manager_));

    // Create the socket interface.
    socket_interface_ = std::make_unique<ReverseTunnelInitiator>(context_);

    // Create the extension.
    extension_ = std::make_unique<ReverseTunnelInitiatorExtension>(context_, config_);
  }

  // Helper function to set up thread local slot for tests.
  void setupThreadLocalSlot() {
    // Create a thread local registry.
    thread_local_registry_ =
        std::make_shared<DownstreamSocketThreadLocal>(dispatcher_, *stats_scope_);

    // Create the actual TypedSlot.
    tls_slot_ = ThreadLocal::TypedSlot<DownstreamSocketThreadLocal>::makeUnique(thread_local_);
    thread_local_.setDispatcher(&dispatcher_);

    // Set up the slot to return our registry.
    tls_slot_->set([registry = thread_local_registry_](Event::Dispatcher&) { return registry; });

    // Set the slot in the extension using the test-only method.
    extension_->setTestOnlyTLSRegistry(std::move(tls_slot_));
  }

  void setupAnotherThreadLocalSlot() {
    // Create a thread local registry for the other dispatcher.
    another_thread_local_registry_ =
        std::make_shared<DownstreamSocketThreadLocal>(dispatcher_, *stats_scope_);

    // Create the actual TypedSlot.
    another_tls_slot_ =
        ThreadLocal::TypedSlot<DownstreamSocketThreadLocal>::makeUnique(thread_local_);
    thread_local_.setDispatcher(&dispatcher_);

    // Set up the slot to return our registry.
    another_tls_slot_->set(
        [registry = another_thread_local_registry_](Event::Dispatcher&) { return registry; });

    // Set the slot in the extension using the test-only method.
    extension_->setTestOnlyTLSRegistry(std::move(another_tls_slot_));
  }

  void TearDown() override {
    tls_slot_.reset();
    thread_local_registry_.reset();
    extension_.reset();
    socket_interface_.reset();
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<ThreadLocal::MockInstance> thread_local_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopeSharedPtr stats_scope_;
  NiceMock<Event::MockDispatcher> dispatcher_{"worker_0"};

  envoy::extensions::bootstrap::reverse_tunnel::downstream_socket_interface::v3::
      DownstreamReverseConnectionSocketInterface config_;

  std::unique_ptr<ReverseTunnelInitiator> socket_interface_;
  std::unique_ptr<ReverseTunnelInitiatorExtension> extension_;

  std::unique_ptr<ThreadLocal::TypedSlot<DownstreamSocketThreadLocal>> tls_slot_;
  std::shared_ptr<DownstreamSocketThreadLocal> thread_local_registry_;
  std::unique_ptr<ThreadLocal::TypedSlot<DownstreamSocketThreadLocal>> another_tls_slot_;
  std::shared_ptr<DownstreamSocketThreadLocal> another_thread_local_registry_;
};

// Basic functionality tests.
TEST_F(ReverseTunnelInitiatorExtensionTest, InitializeWithDefaultConfig) {
  // Test with empty config (should initialize successfully).
  envoy::extensions::bootstrap::reverse_tunnel::downstream_socket_interface::v3::
      DownstreamReverseConnectionSocketInterface empty_config;

  auto extension_with_default =
      std::make_unique<ReverseTunnelInitiatorExtension>(context_, empty_config);

  EXPECT_NE(extension_with_default, nullptr);
}

TEST_F(ReverseTunnelInitiatorExtensionTest, OnServerInitialized) {
  // This should be a no-op.
  extension_->onServerInitialized();
}

TEST_F(ReverseTunnelInitiatorExtensionTest, OnWorkerThreadInitialized) {
  // Test that onWorkerThreadInitialized creates thread local slot.
  extension_->onWorkerThreadInitialized();

  // Verify that the thread local slot was created by checking getLocalRegistry.
  EXPECT_NE(extension_->getLocalRegistry(), nullptr);
}

// Thread local registry access tests.
TEST_F(ReverseTunnelInitiatorExtensionTest, GetLocalRegistryBeforeInitialization) {
  // Before tls_slot_ is set, getLocalRegistry should return nullptr.
  EXPECT_EQ(extension_->getLocalRegistry(), nullptr);
}

TEST_F(ReverseTunnelInitiatorExtensionTest, GetLocalRegistryAfterInitialization) {

  // First test with uninitialized TLS.
  EXPECT_EQ(extension_->getLocalRegistry(), nullptr);

  // Initialize the thread local slot.
  setupThreadLocalSlot();

  // Now getLocalRegistry should return the actual registry.
  auto* registry = extension_->getLocalRegistry();
  EXPECT_NE(registry, nullptr);
  EXPECT_EQ(registry, thread_local_registry_.get());

  // Test multiple calls return same registry.
  auto* registry2 = extension_->getLocalRegistry();
  EXPECT_EQ(registry, registry2);
}

TEST_F(ReverseTunnelInitiatorExtensionTest, GetStatsScope) {
  // Test that getStatsScope returns the correct scope.
  EXPECT_EQ(&extension_->getStatsScope(), stats_scope_.get());
}

TEST_F(ReverseTunnelInitiatorExtensionTest, DownstreamSocketThreadLocalScope) {
  // Set up thread local slot first.
  setupThreadLocalSlot();

  // Get the thread local registry.
  auto* registry = extension_->getLocalRegistry();
  EXPECT_NE(registry, nullptr);

  // Test that the scope() method returns the correct scope.
  EXPECT_EQ(&registry->scope(), stats_scope_.get());
}

TEST_F(ReverseTunnelInitiatorExtensionTest, UpdateConnectionStatsIncrement) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  // Test updateConnectionStats with increment=true.
  std::string node_id = "test-node-123";
  std::string cluster_id = "test-cluster-456";
  std::string state_suffix = "connecting";

  // Call updateConnectionStats to increment.
  extension_->updateConnectionStats(node_id, cluster_id, state_suffix, true);

  // Verify that the correct stats were created and incremented using cross-worker stat map.
  auto stat_map = extension_->getCrossWorkerStatMap();

  std::string expected_node_stat =
      fmt::format("test_scope.reverse_connections.host.{}.{}", node_id, state_suffix);
  std::string expected_cluster_stat =
      fmt::format("test_scope.reverse_connections.cluster.{}.{}", cluster_id, state_suffix);

  EXPECT_EQ(stat_map[expected_node_stat], 1);
  EXPECT_EQ(stat_map[expected_cluster_stat], 1);
}

TEST_F(ReverseTunnelInitiatorExtensionTest, UpdateConnectionStatsDecrement) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  // Test updateConnectionStats with increment=false.
  std::string node_id = "test-node-789";
  std::string cluster_id = "test-cluster-012";
  std::string state_suffix = "connected";

  // First increment to have something to decrement.
  extension_->updateConnectionStats(node_id, cluster_id, state_suffix, true);
  extension_->updateConnectionStats(node_id, cluster_id, state_suffix, true);

  // Verify incremented values using cross-worker stat map.
  auto stat_map = extension_->getCrossWorkerStatMap();
  std::string expected_node_stat =
      fmt::format("test_scope.reverse_connections.host.{}.{}", node_id, state_suffix);
  std::string expected_cluster_stat =
      fmt::format("test_scope.reverse_connections.cluster.{}.{}", cluster_id, state_suffix);

  EXPECT_EQ(stat_map[expected_node_stat], 2);
  EXPECT_EQ(stat_map[expected_cluster_stat], 2);

  // Now decrement.
  extension_->updateConnectionStats(node_id, cluster_id, state_suffix, false);

  // Get updated stats after decrement.
  stat_map = extension_->getCrossWorkerStatMap();

  EXPECT_EQ(stat_map[expected_node_stat], 1);
  EXPECT_EQ(stat_map[expected_cluster_stat], 1);
}

TEST_F(ReverseTunnelInitiatorExtensionTest, UpdateConnectionStatsMultipleStates) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  // Test updateConnectionStats with multiple different states.
  std::string node_id = "test-node-multi";
  std::string cluster_id = "test-cluster-multi";

  // Create stats for different states.
  extension_->updateConnectionStats(node_id, cluster_id, "connecting", true);
  extension_->updateConnectionStats(node_id, cluster_id, "connected", true);
  extension_->updateConnectionStats(node_id, cluster_id, "failed", true);

  // Verify all states have separate gauges using cross-worker stat map.
  auto stat_map = extension_->getCrossWorkerStatMap();

  EXPECT_EQ(stat_map[fmt::format("test_scope.reverse_connections.host.{}.connecting", node_id)], 1);
  EXPECT_EQ(stat_map[fmt::format("test_scope.reverse_connections.host.{}.connected", node_id)], 1);
  EXPECT_EQ(stat_map[fmt::format("test_scope.reverse_connections.host.{}.failed", node_id)], 1);
}

TEST_F(ReverseTunnelInitiatorExtensionTest, UpdateConnectionStatsEmptyValues) {
  // Test updateConnectionStats with empty values - should not update stats.
  auto& stats_store = extension_->getStatsScope();

  // Empty host_id - should not create/update stats.
  extension_->updateConnectionStats("", "test-cluster", "connecting", true);
  auto& empty_host_gauge = stats_store.gaugeFromString("reverse_connections.host..connecting",
                                                       Stats::Gauge::ImportMode::Accumulate);
  EXPECT_EQ(empty_host_gauge.value(), 0);

  // Empty cluster_id - should not create/update stats.
  extension_->updateConnectionStats("test-host", "", "connecting", true);
  auto& empty_cluster_gauge = stats_store.gaugeFromString("reverse_connections.cluster..connecting",
                                                          Stats::Gauge::ImportMode::Accumulate);
  EXPECT_EQ(empty_cluster_gauge.value(), 0);

  // Empty state_suffix - should not create/update stats.
  extension_->updateConnectionStats("test-host", "test-cluster", "", true);
  auto& empty_state_gauge = stats_store.gaugeFromString("reverse_connections.host.test-host.",
                                                        Stats::Gauge::ImportMode::Accumulate);
  EXPECT_EQ(empty_state_gauge.value(), 0);
}

// Test per-worker stats aggregation for one thread only (test thread)
TEST_F(ReverseTunnelInitiatorExtensionTest, GetPerWorkerStatMapSingleThread) {
  // Set up thread local slot first.
  setupThreadLocalSlot();

  // Update per-worker stats for the current (test) thread.
  extension_->updatePerWorkerConnectionStats("host1", "cluster1", "connecting", true);
  extension_->updatePerWorkerConnectionStats("host2", "cluster2", "connected", true);
  extension_->updatePerWorkerConnectionStats("host2", "cluster2", "connected", true);

  // Get the per-worker stat map.
  auto stat_map = extension_->getPerWorkerStatMap();

  // Verify the stats are collected correctly for worker_0.
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.host.host1.connecting"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.host.host2.connected"], 2);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.cluster.cluster1.connecting"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.cluster.cluster2.connected"], 2);

  // Verify that only worker_0 stats are included.
  for (const auto& [stat_name, value] : stat_map) {
    EXPECT_TRUE(stat_name.find("worker_0") != std::string::npos);
  }
}

// Test cross-thread stat map functions using multiple dispatchers.
TEST_F(ReverseTunnelInitiatorExtensionTest, GetCrossWorkerStatMapMultiThread) {
  // Set up thread local slot for the test thread (dispatcher name: "worker_0")
  setupThreadLocalSlot();

  // Set up another thread local slot for a different dispatcher (dispatcher name: "worker_1")
  setupAnotherThreadLocalSlot();

  // Simulate stats updates from worker_0.
  extension_->updateConnectionStats("host1", "cluster1", "connecting", true);
  extension_->updateConnectionStats("host1", "cluster1", "connecting", true); // Increment twice
  extension_->updateConnectionStats("host2", "cluster2", "connected", true);

  // Temporarily switch the thread local registry to simulate updates from worker_1.
  auto original_registry = thread_local_registry_;
  thread_local_registry_ = another_thread_local_registry_;

  // Update stats from worker_1.
  extension_->updateConnectionStats("host1", "cluster1", "connecting",
                                    true);                                // Increment from worker_1
  extension_->updateConnectionStats("host3", "cluster3", "failed", true); // New host from worker_1

  // Restore the original registry.
  thread_local_registry_ = original_registry;

  // Get the cross-worker stat map.
  auto stat_map = extension_->getCrossWorkerStatMap();

  // Verify that cross-worker stats are collected correctly across multiple dispatchers.
  // host1: incremented 3 times total (2 from worker_0 + 1 from worker_1)
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.host1.connecting"], 3);
  // host2: incremented 1 time from worker_0
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.host2.connected"], 1);
  // host3: incremented 1 time from worker_1
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.host3.failed"], 1);

  // cluster1: incremented 3 times total (2 from worker_0 + 1 from worker_1)
  EXPECT_EQ(stat_map["test_scope.reverse_connections.cluster.cluster1.connecting"], 3);
  // cluster2: incremented 1 time from worker_0
  EXPECT_EQ(stat_map["test_scope.reverse_connections.cluster.cluster2.connected"], 1);
  // cluster3: incremented 1 time from worker_1
  EXPECT_EQ(stat_map["test_scope.reverse_connections.cluster.cluster3.failed"], 1);

  // Test StatNameManagedStorage behavior: verify that calling updateConnectionStats again.
  // with the same names increments the existing gauges (not creates new ones)
  extension_->updateConnectionStats("host1", "cluster1", "connecting", true); // Increment again
  extension_->updateConnectionStats("host2", "cluster2", "connected", false); // Decrement to 0

  // Get stats again to verify the same gauges were updated.
  stat_map = extension_->getCrossWorkerStatMap();

  // Verify the gauge values were updated correctly (StatNameManagedStorage ensures same gauge)
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.host1.connecting"], 4);       // 3 + 1
  EXPECT_EQ(stat_map["test_scope.reverse_connections.cluster.cluster1.connecting"], 4); // 3 + 1
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.host2.connected"], 0);        // 1 - 1
  EXPECT_EQ(stat_map["test_scope.reverse_connections.cluster.cluster2.connected"], 0);  // 1 - 1
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.host3.failed"], 1);           // unchanged
  EXPECT_EQ(stat_map["test_scope.reverse_connections.cluster.cluster3.failed"], 1);     // unchanged

  // Test per-worker decrement operations to cover the per-worker decrement code paths.
  // First, test decrements from worker_0 context.
  extension_->updatePerWorkerConnectionStats("host1", "cluster1", "connecting",
                                             false); // Decrement from worker_0

  // Get per-worker stats to verify decrements worked correctly for worker_0.
  auto per_worker_stat_map = extension_->getPerWorkerStatMap();

  // Verify worker_0 stats were decremented correctly.
  EXPECT_EQ(per_worker_stat_map["test_scope.reverse_connections.worker_0.host.host1.connecting"],
            3); // 4 - 1
  EXPECT_EQ(
      per_worker_stat_map["test_scope.reverse_connections.worker_0.cluster.cluster1.connecting"],
      3); // 4 - 1

  // Now test decrements from worker_1 context.
  thread_local_registry_ = another_thread_local_registry_;

  // Decrement some stats from worker_1.
  extension_->updatePerWorkerConnectionStats("host1", "cluster1", "connecting",
                                             false); // Decrement from worker_1
  extension_->updatePerWorkerConnectionStats("host3", "cluster3", "failed",
                                             false); // Decrement host3 to 0

  // Get per-worker stats from worker_1 context.
  auto worker1_stat_map = extension_->getPerWorkerStatMap();

  // Verify worker_1 stats were decremented correctly.
  EXPECT_EQ(worker1_stat_map["test_scope.reverse_connections.worker_1.host.host1.connecting"],
            0); // 1 - 1
  EXPECT_EQ(worker1_stat_map["test_scope.reverse_connections.worker_1.cluster.cluster1.connecting"],
            0); // 1 - 1
  EXPECT_EQ(worker1_stat_map["test_scope.reverse_connections.worker_1.host.host3.failed"],
            0); // 1 - 1
  EXPECT_EQ(worker1_stat_map["test_scope.reverse_connections.worker_1.cluster.cluster3.failed"],
            0); // 1 - 1

  // Restore original registry.
  thread_local_registry_ = original_registry;
}

// Test getConnectionStatsSync using multiple dispatchers.
TEST_F(ReverseTunnelInitiatorExtensionTest, GetConnectionStatsSyncMultiThread) {
  // Set up thread local slot for the test thread (dispatcher name: "worker_0")
  setupThreadLocalSlot();

  // Set up another thread local slot for a different dispatcher (dispatcher name: "worker_1")
  setupAnotherThreadLocalSlot();

  // Simulate stats updates from worker_0.
  extension_->updateConnectionStats("host1", "cluster1", "connected", true);
  extension_->updateConnectionStats("host1", "cluster1", "connected", true); // Increment twice
  extension_->updateConnectionStats("host2", "cluster2", "connected", true);

  // Simulate stats updates from worker_1.
  // Temporarily switch the thread local registry to simulate the other dispatcher.
  auto original_registry = thread_local_registry_;
  thread_local_registry_ = another_thread_local_registry_;

  // Update stats from worker_1.
  extension_->updateConnectionStats("host1", "cluster1", "connected",
                                    true); // Increment from worker_1
  extension_->updateConnectionStats("host3", "cluster3", "connected",
                                    true); // New host from worker_1

  // Restore the original registry.
  thread_local_registry_ = original_registry;

  // Get connection stats synchronously.
  auto result = extension_->getConnectionStatsSync(std::chrono::milliseconds(100));
  auto& [connected_nodes, accepted_connections] = result;

  // Verify the result contains the expected data.
  EXPECT_FALSE(connected_nodes.empty() || accepted_connections.empty());

  // Verify that we have the expected host and cluster data.
  // host1: should be present (incremented 3 times total)
  EXPECT_TRUE(std::find(connected_nodes.begin(), connected_nodes.end(), "host1") !=
              connected_nodes.end());
  // host2: should be present (incremented 1 time)
  EXPECT_TRUE(std::find(connected_nodes.begin(), connected_nodes.end(), "host2") !=
              connected_nodes.end());
  // host3: should be present (incremented 1 time)
  EXPECT_TRUE(std::find(connected_nodes.begin(), connected_nodes.end(), "host3") !=
              connected_nodes.end());

  // cluster1: should be present (incremented 3 times total)
  EXPECT_TRUE(std::find(accepted_connections.begin(), accepted_connections.end(), "cluster1") !=
              accepted_connections.end());
  // cluster2: should be present (incremented 1 time)
  EXPECT_TRUE(std::find(accepted_connections.begin(), accepted_connections.end(), "cluster2") !=
              accepted_connections.end());
  // cluster3: should be present (incremented 1 time)
  EXPECT_TRUE(std::find(accepted_connections.begin(), accepted_connections.end(), "cluster3") !=
              accepted_connections.end());

  // Test StatNameManagedStorage behavior: verify that calling updateConnectionStats again.
  // with the same names updates the existing gauges and the sync result reflects this
  extension_->updateConnectionStats("host1", "cluster1", "connected", true);  // Increment again
  extension_->updateConnectionStats("host2", "cluster2", "connected", false); // Decrement to 0

  // Get connection stats again to verify the updated values.
  result = extension_->getConnectionStatsSync(std::chrono::milliseconds(100));
  auto& [updated_connected_nodes, updated_accepted_connections] = result;

  // Verify that host2 is no longer present (gauge value is 0)
  EXPECT_TRUE(std::find(updated_connected_nodes.begin(), updated_connected_nodes.end(), "host2") ==
              updated_connected_nodes.end());
  EXPECT_TRUE(std::find(updated_accepted_connections.begin(), updated_accepted_connections.end(),
                        "cluster2") == updated_accepted_connections.end());

  // Verify that host1 and host3 are still present.
  EXPECT_TRUE(std::find(updated_connected_nodes.begin(), updated_connected_nodes.end(), "host1") !=
              updated_connected_nodes.end());
  EXPECT_TRUE(std::find(updated_connected_nodes.begin(), updated_connected_nodes.end(), "host3") !=
              updated_connected_nodes.end());
  EXPECT_TRUE(std::find(updated_accepted_connections.begin(), updated_accepted_connections.end(),
                        "cluster1") != updated_accepted_connections.end());
  EXPECT_TRUE(std::find(updated_accepted_connections.begin(), updated_accepted_connections.end(),
                        "cluster3") != updated_accepted_connections.end());
}

// Test getConnectionStatsSync with timeouts.
TEST_F(ReverseTunnelInitiatorExtensionTest, GetConnectionStatsSyncTimeout) {
  // Test with a very short timeout to verify timeout behavior.
  auto result = extension_->getConnectionStatsSync(std::chrono::milliseconds(1));

  // With no connections and short timeout, should return empty results.
  auto& [connected_nodes, accepted_connections] = result;
  EXPECT_TRUE(connected_nodes.empty());
  EXPECT_TRUE(accepted_connections.empty());
}

// Test getConnectionStatsSync filters only "connected" state.
TEST_F(ReverseTunnelInitiatorExtensionTest, GetConnectionStatsSyncFiltersConnectedState) {
  // Set up thread local slot.
  setupThreadLocalSlot();

  // Add connections with different states.
  extension_->updateConnectionStats("host1", "cluster1", "connecting", true);
  extension_->updateConnectionStats("host2", "cluster2", "connected", true);
  extension_->updateConnectionStats("host3", "cluster3", "failed", true);
  extension_->updateConnectionStats("host4", "cluster4", "connected", true);

  // Get connection stats synchronously.
  auto result = extension_->getConnectionStatsSync(std::chrono::milliseconds(100));
  auto& [connected_nodes, accepted_connections] = result;

  // Should only include hosts/clusters with "connected" state.
  EXPECT_EQ(connected_nodes.size(), 2);
  EXPECT_EQ(accepted_connections.size(), 2);

  // Verify only connected hosts are included.
  EXPECT_TRUE(std::find(connected_nodes.begin(), connected_nodes.end(), "host2") !=
              connected_nodes.end());
  EXPECT_TRUE(std::find(connected_nodes.begin(), connected_nodes.end(), "host4") !=
              connected_nodes.end());

  // Verify connecting and failed hosts are NOT included.
  EXPECT_TRUE(std::find(connected_nodes.begin(), connected_nodes.end(), "host1") ==
              connected_nodes.end());
  EXPECT_TRUE(std::find(connected_nodes.begin(), connected_nodes.end(), "host3") ==
              connected_nodes.end());

  // Verify only connected clusters are included.
  EXPECT_TRUE(std::find(accepted_connections.begin(), accepted_connections.end(), "cluster2") !=
              accepted_connections.end());
  EXPECT_TRUE(std::find(accepted_connections.begin(), accepted_connections.end(), "cluster4") !=
              accepted_connections.end());

  // Verify connecting and failed clusters are NOT included.
  EXPECT_TRUE(std::find(accepted_connections.begin(), accepted_connections.end(), "cluster1") ==
              accepted_connections.end());
  EXPECT_TRUE(std::find(accepted_connections.begin(), accepted_connections.end(), "cluster3") ==
              accepted_connections.end());
}

// Configuration validation tests.
class ConfigValidationTest : public testing::Test {
protected:
  envoy::extensions::bootstrap::reverse_tunnel::downstream_socket_interface::v3::
      DownstreamReverseConnectionSocketInterface config_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<ThreadLocal::MockInstance> thread_local_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopeSharedPtr stats_scope_;

  ConfigValidationTest() {
    stats_scope_ = Stats::ScopeSharedPtr(stats_store_.createScope("test_scope."));
    EXPECT_CALL(context_, threadLocal()).WillRepeatedly(ReturnRef(thread_local_));
    EXPECT_CALL(context_, scope()).WillRepeatedly(ReturnRef(*stats_scope_));
    EXPECT_CALL(context_, clusterManager()).WillRepeatedly(ReturnRef(cluster_manager_));
  }
};

TEST_F(ConfigValidationTest, ValidConfiguration) {
  // Test that valid configuration gets accepted.
  ReverseTunnelInitiator initiator(context_);

  // Should not throw when creating bootstrap extension.
  EXPECT_NO_THROW(initiator.createBootstrapExtension(config_, context_));
}

TEST_F(ConfigValidationTest, EmptyConfiguration) {
  // Test that empty configuration still works.
  ReverseTunnelInitiator initiator(context_);

  // Should not throw with empty config.
  EXPECT_NO_THROW(initiator.createBootstrapExtension(config_, context_));
}

TEST_F(ConfigValidationTest, EmptyStatPrefix) {
  // Test that empty stat_prefix still works with default.
  ReverseTunnelInitiator initiator(context_);

  // Should not throw and should use default prefix.
  EXPECT_NO_THROW(initiator.createBootstrapExtension(config_, context_));
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
