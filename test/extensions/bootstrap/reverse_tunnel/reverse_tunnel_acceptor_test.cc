#include "envoy/extensions/bootstrap/reverse_connection_socket_interface/v3/upstream_reverse_connection_socket_interface.pb.h"
#include "envoy/network/socket_interface.h"
#include "envoy/server/factory_context.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/common/network/utility.h"
#include "source/common/thread_local/thread_local_impl.h"
#include "source/extensions/bootstrap/reverse_connection_socket_interface/reverse_tunnel_acceptor.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/test_runtime.h"

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

class ReverseTunnelAcceptorExtensionTest : public testing::Test {
protected:
  ReverseTunnelAcceptorExtensionTest() {
    // Set up the stats scope.
    stats_scope_ = Stats::ScopeSharedPtr(stats_store_.createScope("test_scope."));

    // Set up the mock context.
    EXPECT_CALL(context_, threadLocal()).WillRepeatedly(ReturnRef(thread_local_));
    EXPECT_CALL(context_, scope()).WillRepeatedly(ReturnRef(*stats_scope_));

    // Create the config.
    config_.set_stat_prefix("test_prefix");

    // Create the socket interface.
    socket_interface_ = std::make_unique<ReverseTunnelAcceptor>(context_);

    // Create the extension.
    extension_ =
        std::make_unique<ReverseTunnelAcceptorExtension>(*socket_interface_, context_, config_);
  }

  // Helper function to set up thread local slot for tests.
  void setupThreadLocalSlot() {
    // Create a thread local registry.
    thread_local_registry_ =
        std::make_shared<UpstreamSocketThreadLocal>(dispatcher_, extension_.get());

    // Create the actual TypedSlot
    tls_slot_ = ThreadLocal::TypedSlot<UpstreamSocketThreadLocal>::makeUnique(thread_local_);
    thread_local_.setDispatcher(&dispatcher_);

    // Set up the slot to return our registry
    tls_slot_->set([registry = thread_local_registry_](Event::Dispatcher&) { return registry; });

    // Set the slot directly in the extension
    extension_->tls_slot_ = std::move(tls_slot_);

    // Set the extension reference in the socket interface
    extension_->socket_interface_->extension_ = extension_.get();
  }

  // Helper function to set up a second thread local slot for multi-dispatcher testing
  void setupAnotherThreadLocalSlot() {
    // Create another thread local registry with a different dispatcher name
    another_thread_local_registry_ =
        std::make_shared<UpstreamSocketThreadLocal>(another_dispatcher_, extension_.get());
  }

  void TearDown() override {
    tls_slot_.reset();
    thread_local_registry_.reset();
    extension_.reset();
    socket_interface_.reset();
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<ThreadLocal::MockInstance> thread_local_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopeSharedPtr stats_scope_;
  NiceMock<Event::MockDispatcher> dispatcher_{"worker_0"};

  envoy::extensions::bootstrap::reverse_connection_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface config_;

  std::unique_ptr<ReverseTunnelAcceptor> socket_interface_;
  std::unique_ptr<ReverseTunnelAcceptorExtension> extension_;

  // Real thread local slot and registry
  std::unique_ptr<ThreadLocal::TypedSlot<UpstreamSocketThreadLocal>> tls_slot_;
  std::shared_ptr<UpstreamSocketThreadLocal> thread_local_registry_;

  // Additional mock dispatcher and registry for multi-thread testing
  NiceMock<Event::MockDispatcher> another_dispatcher_{"worker_1"};
  std::shared_ptr<UpstreamSocketThreadLocal> another_thread_local_registry_;
};

// Basic functionality tests
TEST_F(ReverseTunnelAcceptorExtensionTest, InitializeWithDefaultStatPrefix) {
  // Test with empty config (should use default stat prefix)
  envoy::extensions::bootstrap::reverse_connection_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface empty_config;

  auto extension_with_default =
      std::make_unique<ReverseTunnelAcceptorExtension>(*socket_interface_, context_, empty_config);

  EXPECT_EQ(extension_with_default->statPrefix(), "upstream_reverse_connection");
}

TEST_F(ReverseTunnelAcceptorExtensionTest, InitializeWithCustomStatPrefix) {
  // Test with custom stat prefix
  EXPECT_EQ(extension_->statPrefix(), "test_prefix");
}

TEST_F(ReverseTunnelAcceptorExtensionTest, GetStatsScope) {
  // Test that getStatsScope returns the correct scope
  EXPECT_EQ(&extension_->getStatsScope(), stats_scope_.get());
}

TEST_F(ReverseTunnelAcceptorExtensionTest, OnWorkerThreadInitialized) {
  // This should be a no-op
  extension_->onWorkerThreadInitialized();
}

// Thread local initialization tests
TEST_F(ReverseTunnelAcceptorExtensionTest, OnServerInitializedSetsExtensionReference) {
  // Call onServerInitialized to set the extension reference in the socket interface
  extension_->onServerInitialized();

  // Verify that the socket interface extension reference is set
  EXPECT_EQ(socket_interface_->getExtension(), extension_.get());
}

// Thread local registry access tests
TEST_F(ReverseTunnelAcceptorExtensionTest, GetLocalRegistryBeforeInitialization) {
  // Before tls_slot_ is set, getLocalRegistry should return nullptr
  EXPECT_EQ(extension_->getLocalRegistry(), nullptr);
}

TEST_F(ReverseTunnelAcceptorExtensionTest, GetLocalRegistryAfterInitialization) {
  // Initialize the thread local slot
  setupThreadLocalSlot();

  // Now getLocalRegistry should return the actual registry
  auto* registry = extension_->getLocalRegistry();
  EXPECT_NE(registry, nullptr);

  // Verify we can access the socket manager from the registry (non-const version)
  auto* socket_manager = registry->socketManager();
  EXPECT_NE(socket_manager, nullptr);

  // Verify the socket manager has the correct extension reference
  EXPECT_EQ(socket_manager->getUpstreamExtension(), extension_.get());

  // Test const socketManager()
  const auto* const_registry = extension_->getLocalRegistry();
  EXPECT_NE(const_registry, nullptr);

  const auto* const_socket_manager = const_registry->socketManager();
  EXPECT_NE(const_socket_manager, nullptr);

  // Verify the const socket manager has the correct extension reference
  EXPECT_EQ(const_socket_manager->getUpstreamExtension(), extension_.get());
}

// Test stats aggregation for one thread only (test thread)
TEST_F(ReverseTunnelAcceptorExtensionTest, GetPerWorkerStatMapSingleThread) {

  // Set up thread local slot first
  setupThreadLocalSlot();

  // Update per-worker stats for the current (test) thread
  extension_->updatePerWorkerConnectionStats("node1", "cluster1", true);
  extension_->updatePerWorkerConnectionStats("node2", "cluster2", true);
  extension_->updatePerWorkerConnectionStats("node2", "cluster2", true);

  // Get the per-worker stat map
  auto stat_map = extension_->getPerWorkerStatMap();

  // Verify the stats are collected correctly for worker_0
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.node.node1"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.node.node2"], 2);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.cluster.cluster1"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.cluster.cluster2"], 2);

  // Verify that only worker_0 stats are included
  for (const auto& [stat_name, value] : stat_map) {
    EXPECT_TRUE(stat_name.find("worker_0") != std::string::npos);
  }

  // Test StatNameManagedStorage behavior: verify that calling updateConnectionStats
  // creates the same gauges and increments them correctly
  extension_->updateConnectionStats("node1", "cluster1", true);
  extension_->updateConnectionStats("node1", "cluster1", true);

  // Get stats again to verify the same gauges were incremented
  stat_map = extension_->getPerWorkerStatMap();

  // Verify the gauge values were incremented correctly
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.node.node1"], 3);       // 1 + 2
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.cluster.cluster1"], 3); // 1 + 2
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.node.node2"], 2);       // unchanged
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.cluster.cluster2"], 2); // unchanged

  // Test decrement operations to cover the decrement code paths
  extension_->updatePerWorkerConnectionStats("node1", "cluster1", false); // Decrement node1
  extension_->updatePerWorkerConnectionStats("node2", "cluster2", false); // Decrement node2 once
  extension_->updatePerWorkerConnectionStats("node2", "cluster2", false); // Decrement node2 again

  // Get stats again to verify the decrements worked correctly
  stat_map = extension_->getPerWorkerStatMap();

  // Verify the gauge values were decremented correctly
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.node.node1"], 2);       // 3 - 1
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.cluster.cluster1"], 2); // 3 - 1
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.node.node2"], 0);       // 2 - 2
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.cluster.cluster2"], 0); // 2 - 2
}

// Test cross-thread stat map functions using multiple dispatchers
TEST_F(ReverseTunnelAcceptorExtensionTest, GetCrossWorkerStatMapMultiThread) {
  // Set up thread local slot for the test thread (dispatcher name: "worker_0")
  setupThreadLocalSlot();

  // Set up another thread local slot for a different dispatcher (dispatcher name: "worker_1")
  setupAnotherThreadLocalSlot();

  // Simulate stats updates from worker_0
  extension_->updateConnectionStats("node1", "cluster1", true);
  extension_->updateConnectionStats("node1", "cluster1", true); // Increment twice
  extension_->updateConnectionStats("node2", "cluster2", true);

  // Simulate stats updates from worker_1
  // Temporarily switch the thread local registry to simulate the other dispatcher
  auto original_registry = thread_local_registry_;
  thread_local_registry_ = another_thread_local_registry_;

  // Update stats from worker_1
  extension_->updateConnectionStats("node1", "cluster1", true); // Increment from worker_1
  extension_->updateConnectionStats("node3", "cluster3", true); // New node from worker_1

  // Restore the original registry
  thread_local_registry_ = original_registry;

  // Get the cross-worker stat map
  auto stat_map = extension_->getCrossWorkerStatMap();

  // Verify that cross-worker stats are collected correctly across multiple dispatchers
  // node1: incremented 3 times total (2 from worker_0 + 1 from worker_1)
  EXPECT_EQ(stat_map["test_scope.reverse_connections.nodes.node1"], 3);
  // node2: incremented 1 time from worker_0
  EXPECT_EQ(stat_map["test_scope.reverse_connections.nodes.node2"], 1);
  // node3: incremented 1 time from worker_1
  EXPECT_EQ(stat_map["test_scope.reverse_connections.nodes.node3"], 1);

  // cluster1: incremented 3 times total (2 from worker_0 + 1 from worker_1)
  EXPECT_EQ(stat_map["test_scope.reverse_connections.clusters.cluster1"], 3);
  // cluster2: incremented 1 time from worker_0
  EXPECT_EQ(stat_map["test_scope.reverse_connections.clusters.cluster2"], 1);
  // cluster3: incremented 1 time from worker_1
  EXPECT_EQ(stat_map["test_scope.reverse_connections.clusters.cluster3"], 1);

  // Test StatNameManagedStorage behavior: verify that calling updateConnectionStats again
  // with the same names increments the existing gauges (not creates new ones)
  extension_->updateConnectionStats("node1", "cluster1", true);  // Increment again
  extension_->updateConnectionStats("node2", "cluster2", false); // Decrement

  // Get stats again to verify the same gauges were updated
  stat_map = extension_->getCrossWorkerStatMap();

  // Verify the gauge values were updated correctly (StatNameManagedStorage ensures same gauge)
  EXPECT_EQ(stat_map["test_scope.reverse_connections.nodes.node1"], 4);       // 3 + 1
  EXPECT_EQ(stat_map["test_scope.reverse_connections.clusters.cluster1"], 4); // 3 + 1
  EXPECT_EQ(stat_map["test_scope.reverse_connections.nodes.node2"], 0);       // 1 - 1
  EXPECT_EQ(stat_map["test_scope.reverse_connections.clusters.cluster2"], 0); // 1 - 1
  EXPECT_EQ(stat_map["test_scope.reverse_connections.nodes.node3"], 1);       // unchanged
  EXPECT_EQ(stat_map["test_scope.reverse_connections.clusters.cluster3"], 1); // unchanged

  // Test per-worker decrement operations to cover the per-worker decrement code paths
  // First, test decrements from worker_0 context
  extension_->updatePerWorkerConnectionStats("node1", "cluster1", false); // Decrement from worker_0

  // Get per-worker stats to verify decrements worked correctly for worker_0
  auto per_worker_stat_map = extension_->getPerWorkerStatMap();

  // Verify worker_0 stats were decremented correctly
  EXPECT_EQ(per_worker_stat_map["test_scope.reverse_connections.worker_0.node.node1"], 3); // 4 - 1
  EXPECT_EQ(per_worker_stat_map["test_scope.reverse_connections.worker_0.cluster.cluster1"],
            3); // 4 - 1

  // Decrement cluster2 which is already at 0 from cross-worker stats
  extension_->updateConnectionStats("node2", "cluster2", false);

  // Get cross-worker stats to verify the guardrail worked
  auto cross_worker_stat_map = extension_->getCrossWorkerStatMap();

  // Verify that cluster2 remains at 0 (guardrail prevented underflow)
  EXPECT_EQ(cross_worker_stat_map["test_scope.reverse_connections.clusters.cluster2"], 0);

  per_worker_stat_map = extension_->getPerWorkerStatMap();

  // Verify that node2/cluster2 remain at 0 (not wrapped around to UINT64_MAX)
  EXPECT_EQ(per_worker_stat_map["test_scope.reverse_connections.worker_0.node.node2"], 0);
  EXPECT_EQ(per_worker_stat_map["test_scope.reverse_connections.worker_0.cluster.cluster2"], 0);

  // Now test decrements from worker_1 context
  thread_local_registry_ = another_thread_local_registry_;

  // Decrement some stats from worker_1
  extension_->updatePerWorkerConnectionStats("node1", "cluster1", false); // Decrement from worker_1
  extension_->updatePerWorkerConnectionStats("node3", "cluster3", false); // Decrement node3 to 0

  // Get per-worker stats from worker_1 context
  auto worker1_stat_map = extension_->getPerWorkerStatMap();

  // Verify worker_1 stats were decremented correctly
  EXPECT_EQ(worker1_stat_map["test_scope.reverse_connections.worker_1.node.node1"], 0); // 1 - 1
  EXPECT_EQ(worker1_stat_map["test_scope.reverse_connections.worker_1.cluster.cluster1"],
            0);                                                                         // 1 - 1
  EXPECT_EQ(worker1_stat_map["test_scope.reverse_connections.worker_1.node.node3"], 0); // 1 - 1
  EXPECT_EQ(worker1_stat_map["test_scope.reverse_connections.worker_1.cluster.cluster3"],
            0); // 1 - 1

  // Restore original registry
  thread_local_registry_ = original_registry;
}

// Test getConnectionStatsSync using multiple dispatchers
TEST_F(ReverseTunnelAcceptorExtensionTest, GetConnectionStatsSyncMultiThread) {
  // Set up thread local slot for the test thread (dispatcher name: "worker_0")
  setupThreadLocalSlot();

  // Set up another thread local slot for a different dispatcher (dispatcher name: "worker_1")
  setupAnotherThreadLocalSlot();

  // Simulate stats updates from worker_0
  extension_->updateConnectionStats("node1", "cluster1", true);
  extension_->updateConnectionStats("node1", "cluster1", true); // Increment twice
  extension_->updateConnectionStats("node2", "cluster2", true);

  // Simulate stats updates from worker_1
  // Temporarily switch the thread local registry to simulate the other dispatcher
  auto original_registry = thread_local_registry_;
  thread_local_registry_ = another_thread_local_registry_;

  // Update stats from worker_1
  extension_->updateConnectionStats("node1", "cluster1", true); // Increment from worker_1
  extension_->updateConnectionStats("node3", "cluster3", true); // New node from worker_1

  // Restore the original registry
  thread_local_registry_ = original_registry;

  // Get connection stats synchronously
  auto result = extension_->getConnectionStatsSync();
  auto& [connected_nodes, accepted_connections] = result;

  // Verify the result contains the expected data
  EXPECT_FALSE(connected_nodes.empty() || accepted_connections.empty());

  // Verify that we have the expected node and cluster data
  // node1: should be present (incremented 3 times total)
  EXPECT_TRUE(std::find(connected_nodes.begin(), connected_nodes.end(), "node1") !=
              connected_nodes.end());
  // node2: should be present (incremented 1 time)
  EXPECT_TRUE(std::find(connected_nodes.begin(), connected_nodes.end(), "node2") !=
              connected_nodes.end());
  // node3: should be present (incremented 1 time)
  EXPECT_TRUE(std::find(connected_nodes.begin(), connected_nodes.end(), "node3") !=
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

  // Test StatNameManagedStorage behavior: verify that calling updateConnectionStats again
  // with the same names updates the existing gauges and the sync result reflects this
  extension_->updateConnectionStats("node1", "cluster1", true);  // Increment again
  extension_->updateConnectionStats("node2", "cluster2", false); // Decrement to 0

  // Get connection stats again to verify the updated values
  result = extension_->getConnectionStatsSync();
  auto& [updated_connected_nodes, updated_accepted_connections] = result;

  // Verify that node2 is no longer present (gauge value is 0)
  EXPECT_TRUE(std::find(updated_connected_nodes.begin(), updated_connected_nodes.end(), "node2") ==
              updated_connected_nodes.end());
  EXPECT_TRUE(std::find(updated_accepted_connections.begin(), updated_accepted_connections.end(),
                        "cluster2") == updated_accepted_connections.end());

  // Verify that node1 and node3 are still present
  EXPECT_TRUE(std::find(updated_connected_nodes.begin(), updated_connected_nodes.end(), "node1") !=
              updated_connected_nodes.end());
  EXPECT_TRUE(std::find(updated_connected_nodes.begin(), updated_connected_nodes.end(), "node3") !=
              updated_connected_nodes.end());
  EXPECT_TRUE(std::find(updated_accepted_connections.begin(), updated_accepted_connections.end(),
                        "cluster1") != updated_accepted_connections.end());
  EXPECT_TRUE(std::find(updated_accepted_connections.begin(), updated_accepted_connections.end(),
                        "cluster3") != updated_accepted_connections.end());
}

// Test getConnectionStatsSync with timeouts
TEST_F(ReverseTunnelAcceptorExtensionTest, GetConnectionStatsSyncTimeout) {
  // Test with a very short timeout to verify timeout behavior
  auto result = extension_->getConnectionStatsSync(std::chrono::milliseconds(1));

  // With no connections and short timeout, should return empty results
  auto& [connected_nodes, accepted_connections] = result;
  EXPECT_TRUE(connected_nodes.empty());
  EXPECT_TRUE(accepted_connections.empty());
}

// ============================================================================
// TestUpstreamSocketManager Test Class
// ============================================================================

class TestUpstreamSocketManager : public testing::Test {
protected:
  TestUpstreamSocketManager() {
    // Set up the stats scope
    stats_scope_ = Stats::ScopeSharedPtr(stats_store_.createScope("test_scope."));

    // Set up the mock context
    EXPECT_CALL(context_, threadLocal()).WillRepeatedly(ReturnRef(thread_local_));
    EXPECT_CALL(context_, scope()).WillRepeatedly(ReturnRef(*stats_scope_));

    // Create the config
    config_.set_stat_prefix("test_prefix");

    // Create the socket interface
    socket_interface_ = std::make_unique<ReverseTunnelAcceptor>(context_);

    // Create the extension
    extension_ =
        std::make_unique<ReverseTunnelAcceptorExtension>(*socket_interface_, context_, config_);

    // Set up mock dispatcher with default expectations
    EXPECT_CALL(dispatcher_, createTimer_(_))
        .WillRepeatedly(testing::ReturnNew<NiceMock<Event::MockTimer>>());
    EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, _))
        .WillRepeatedly(testing::ReturnNew<NiceMock<Event::MockFileEvent>>());

    // Create the socket manager with real extension
    socket_manager_ = std::make_unique<UpstreamSocketManager>(dispatcher_, extension_.get());
  }

  void TearDown() override {
    socket_manager_.reset();

    extension_.reset();
    socket_interface_.reset();
  }

  // Helper methods to access private members (friend class works for these methods)
  void verifyInitialState() {
    EXPECT_EQ(socket_manager_->accepted_reverse_connections_.size(), 0);
    EXPECT_EQ(socket_manager_->fd_to_node_map_.size(), 0);
    EXPECT_EQ(socket_manager_->node_to_cluster_map_.size(), 0);
    EXPECT_EQ(socket_manager_->cluster_to_node_map_.size(), 0);
  }

  bool verifyFDToNodeMap(int fd) {
    return socket_manager_->fd_to_node_map_.find(fd) != socket_manager_->fd_to_node_map_.end();
  }

  bool verifyFDToEventMap(int fd) {
    return socket_manager_->fd_to_event_map_.find(fd) != socket_manager_->fd_to_event_map_.end();
  }

  bool verifyFDToTimerMap(int fd) {
    return socket_manager_->fd_to_timer_map_.find(fd) != socket_manager_->fd_to_timer_map_.end();
  }

  size_t getFDToEventMapSize() { return socket_manager_->fd_to_event_map_.size(); }

  size_t getFDToTimerMapSize() { return socket_manager_->fd_to_timer_map_.size(); }

  size_t verifyAcceptedReverseConnectionsMap(const std::string& node_id) {
    auto it = socket_manager_->accepted_reverse_connections_.find(node_id);
    if (it == socket_manager_->accepted_reverse_connections_.end()) {
      return 0;
    }
    return it->second.size();
  }

  std::string getNodeToClusterMapping(const std::string& node_id) {
    auto it = socket_manager_->node_to_cluster_map_.find(node_id);
    if (it == socket_manager_->node_to_cluster_map_.end()) {
      return "";
    }
    return it->second;
  }

  std::vector<std::string> getClusterToNodeMapping(const std::string& cluster_id) {
    auto it = socket_manager_->cluster_to_node_map_.find(cluster_id);
    if (it == socket_manager_->cluster_to_node_map_.end()) {
      return {};
    }
    return it->second;
  }

  size_t getNodeToClusterMapSize() { return socket_manager_->node_to_cluster_map_.size(); }

  size_t getClusterToNodeMapSize() { return socket_manager_->cluster_to_node_map_.size(); }

  size_t getAcceptedReverseConnectionsSize() {
    return socket_manager_->accepted_reverse_connections_.size();
  }

  // Helper methods for the new test cases
  void addNodeToClusterMapping(const std::string& node_id, const std::string& cluster_id) {
    socket_manager_->node_to_cluster_map_[node_id] = cluster_id;
    socket_manager_->cluster_to_node_map_[cluster_id].push_back(node_id);
  }

  void addFDToNodeMapping(int fd, const std::string& node_id) {
    socket_manager_->fd_to_node_map_[fd] = node_id;
  }

  // Helper to create a mock socket with proper address setup
  Network::ConnectionSocketPtr createMockSocket(int fd = 123,
                                                const std::string& local_addr = "127.0.0.1:8080",
                                                const std::string& remote_addr = "127.0.0.1:9090") {
    auto socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();

    // Parse local address (IP:port format)
    auto local_colon_pos = local_addr.find(':');
    std::string local_ip = local_addr.substr(0, local_colon_pos);
    uint32_t local_port = std::stoi(local_addr.substr(local_colon_pos + 1));
    auto local_address = Network::Utility::parseInternetAddressNoThrow(local_ip, local_port);

    // Parse remote address (IP:port format)
    auto remote_colon_pos = remote_addr.find(':');
    std::string remote_ip = remote_addr.substr(0, remote_colon_pos);
    uint32_t remote_port = std::stoi(remote_addr.substr(remote_colon_pos + 1));
    auto remote_address = Network::Utility::parseInternetAddressNoThrow(remote_ip, remote_port);

    // Create a mock IO handle and set it up
    auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
    auto* mock_io_handle_ptr = mock_io_handle.get();
    EXPECT_CALL(*mock_io_handle_ptr, fdDoNotUse()).WillRepeatedly(Return(fd));
    EXPECT_CALL(*socket, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle_ptr));

    // Store the mock_io_handle in the socket
    socket->io_handle_ = std::move(mock_io_handle);

    // Set up connection info provider with the desired addresses
    socket->connection_info_provider_->setLocalAddress(local_address);
    socket->connection_info_provider_->setRemoteAddress(remote_address);

    return socket;
  }

  // Helper to create a mock timer
  Event::MockTimer* createMockTimer() {
    auto timer = new NiceMock<Event::MockTimer>();
    EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Return(timer));
    return timer;
  }

  // Helper to create a mock file event
  Event::MockFileEvent* createMockFileEvent() {
    auto file_event = new NiceMock<Event::MockFileEvent>();
    EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, _)).WillOnce(Return(file_event));
    return file_event;
  }

  // Helper to get sockets for a node
  std::list<Network::ConnectionSocketPtr>& getSocketsForNode(const std::string& node_id) {
    return socket_manager_->accepted_reverse_connections_[node_id];
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<ThreadLocal::MockInstance> thread_local_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopeSharedPtr stats_scope_;
  NiceMock<Event::MockDispatcher> dispatcher_;

  envoy::extensions::bootstrap::reverse_connection_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface config_;

  std::unique_ptr<ReverseTunnelAcceptor> socket_interface_;
  std::unique_ptr<ReverseTunnelAcceptorExtension> extension_;
  std::unique_ptr<UpstreamSocketManager> socket_manager_;
};

TEST_F(TestUpstreamSocketManager, CreateUpstreamSocketManager) {
  // Test that constructor doesn't crash and creates a valid instance
  EXPECT_NE(socket_manager_, nullptr);

  // Test constructor with nullptr extension
  auto socket_manager_no_extension = std::make_unique<UpstreamSocketManager>(dispatcher_, nullptr);
  EXPECT_NE(socket_manager_no_extension, nullptr);
}

TEST_F(TestUpstreamSocketManager, GetUpstreamExtension) {
  // Test that getUpstreamExtension returns the correct extension
  EXPECT_EQ(socket_manager_->getUpstreamExtension(), extension_.get());

  // Test with nullptr extension
  auto socket_manager_no_extension = std::make_unique<UpstreamSocketManager>(dispatcher_, nullptr);
  EXPECT_EQ(socket_manager_no_extension->getUpstreamExtension(), nullptr);
}

TEST_F(TestUpstreamSocketManager, AddConnectionSocketEmptyClusterId) {
  // Test adding a socket with empty cluster_id (should log error and return early without adding
  // socket)
  auto socket = createMockSocket(123);
  const std::string node_id = "test-node";
  const std::string cluster_id = "";
  const std::chrono::seconds ping_interval(30);

  // Verify initial state
  verifyInitialState();

  // Add the socket - should return early and not add anything
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval,
                                       false);

  // Verify nothing was added - all maps should remain empty
  verifyInitialState(); // Should still be in initial state

  // Verify no file events or timers were created
  EXPECT_EQ(getFDToEventMapSize(), 0);
  EXPECT_EQ(getFDToTimerMapSize(), 0);

  // Verify no socket can be retrieved
  auto retrieved_socket = socket_manager_->getConnectionSocket(node_id);
  EXPECT_EQ(retrieved_socket, nullptr); // Should return nullptr because nothing was added
}

TEST_F(TestUpstreamSocketManager, AddConnectionSocketEmptyNodeId) {
  // Test adding a socket with empty node_id (should log error and return early without adding
  // socket)
  auto socket = createMockSocket(456);
  const std::string node_id = "";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  // Verify initial state
  verifyInitialState();

  // Add the socket - should return early and not add anything
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval,
                                       false);

  // Verify nothing was added - all maps should remain empty
  verifyInitialState(); // Should still be in initial state

  // Verify no file events or timers were created
  EXPECT_EQ(getFDToEventMapSize(), 0);
  EXPECT_EQ(getFDToTimerMapSize(), 0);

  // Verify no socket can be retrieved
  auto retrieved_socket = socket_manager_->getConnectionSocket(node_id);
  EXPECT_EQ(retrieved_socket, nullptr); // Should return nullptr because nothing was added
}

TEST_F(TestUpstreamSocketManager, AddAndGetMultipleSocketsSameNode) {
  // Test adding multiple sockets for the same node
  auto socket1 = createMockSocket(123);
  auto socket2 = createMockSocket(456);
  auto socket3 = createMockSocket(789);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  // Verify initial state
  verifyInitialState();

  // Add first socket
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket1), ping_interval,
                                       false);

  // Verify maps after first socket
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 1);
  EXPECT_TRUE(verifyFDToNodeMap(123));
  EXPECT_EQ(getNodeToClusterMapping(node_id), cluster_id);
  auto cluster_nodes = getClusterToNodeMapping(cluster_id);
  EXPECT_EQ(cluster_nodes.size(), 1);
  EXPECT_EQ(cluster_nodes[0], node_id);

  // Add second socket for same node
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket2), ping_interval,
                                       false);

  // Verify maps after second socket (should have 2 sockets for same node, but cluster maps
  // unchanged)
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 2);
  EXPECT_TRUE(verifyFDToNodeMap(456));
  EXPECT_EQ(getNodeToClusterMapping(node_id), cluster_id); // Should still be same cluster
  cluster_nodes = getClusterToNodeMapping(cluster_id);
  EXPECT_EQ(cluster_nodes.size(), 1); // Still 1 node per cluster

  // Add third socket for same node
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket3), ping_interval,
                                       false);

  // Verify maps after third socket
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 3);
  EXPECT_TRUE(verifyFDToNodeMap(789));

  // Verify file events and timers were created for all sockets
  EXPECT_EQ(getFDToEventMapSize(), 3);
  EXPECT_EQ(getFDToTimerMapSize(), 3);

  // Get sockets in FIFO order
  auto retrieved_socket1 = socket_manager_->getConnectionSocket(node_id);
  EXPECT_NE(retrieved_socket1, nullptr);

  // Verify socket count decreased
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 2);

  auto retrieved_socket2 = socket_manager_->getConnectionSocket(node_id);
  EXPECT_NE(retrieved_socket2, nullptr);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 1);

  auto retrieved_socket3 = socket_manager_->getConnectionSocket(node_id);
  EXPECT_NE(retrieved_socket3, nullptr);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 0);

  // No more sockets should be available
  auto retrieved_socket4 = socket_manager_->getConnectionSocket(node_id);
  EXPECT_EQ(retrieved_socket4, nullptr);
}

TEST_F(TestUpstreamSocketManager, AddAndGetSocketsMultipleNodes) {
  // Test adding sockets for different nodes
  auto socket1 = createMockSocket(123);
  auto socket2 = createMockSocket(456);
  const std::string node1 = "node1";
  const std::string node2 = "node2";
  const std::string cluster1 = "cluster1";
  const std::string cluster2 = "cluster2";
  const std::chrono::seconds ping_interval(30);

  // Verify initial state
  verifyInitialState();

  // Add socket for first node
  socket_manager_->addConnectionSocket(node1, cluster1, std::move(socket1), ping_interval, false);

  // Verify maps after first node
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node1), 1);
  EXPECT_EQ(getNodeToClusterMapping(node1), cluster1);
  auto cluster1_nodes = getClusterToNodeMapping(cluster1);
  EXPECT_EQ(cluster1_nodes.size(), 1);
  EXPECT_EQ(cluster1_nodes[0], node1);

  // Add socket for second node
  socket_manager_->addConnectionSocket(node2, cluster2, std::move(socket2), ping_interval, false);

  // Verify maps after second node
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node1), 1);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node2), 1);
  EXPECT_EQ(getNodeToClusterMapping(node1), cluster1);
  EXPECT_EQ(getNodeToClusterMapping(node2), cluster2);
  cluster1_nodes = getClusterToNodeMapping(cluster1);
  EXPECT_EQ(cluster1_nodes.size(), 1);
  EXPECT_EQ(cluster1_nodes[0], node1);
  auto cluster2_nodes = getClusterToNodeMapping(cluster2);
  EXPECT_EQ(cluster2_nodes.size(), 1);
  EXPECT_EQ(cluster2_nodes[0], node2);

  // Verify file events and timers were created for both sockets
  EXPECT_EQ(getFDToEventMapSize(), 2);
  EXPECT_EQ(getFDToTimerMapSize(), 2);

  // Verify both nodes have their sockets
  auto retrieved_socket1 = socket_manager_->getConnectionSocket(node1);
  EXPECT_NE(retrieved_socket1, nullptr);

  // Verify first node's socket count decreased
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node1), 0);

  auto retrieved_socket2 = socket_manager_->getConnectionSocket(node2);
  EXPECT_NE(retrieved_socket2, nullptr);

  // Verify second node's socket count decreased
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node2), 0);
}

TEST_F(TestUpstreamSocketManager, TestGetNodeID) {
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  // Call getNodeID with a cluster ID that has active connections
  // First add a socket to create the cluster mapping and update stats
  auto socket1 = createMockSocket(123);
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket1), ping_interval,
                                       false);

  // Verify the socket was added and mappings are correct
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 1);
  EXPECT_EQ(getNodeToClusterMapping(node_id), cluster_id);
  auto cluster_nodes = getClusterToNodeMapping(cluster_id);
  EXPECT_EQ(cluster_nodes.size(), 1);
  EXPECT_EQ(cluster_nodes[0], node_id);

  // Now call getNodeID with the cluster_id - should return the node_id that was added for this
  // cluster
  std::string result_for_cluster = socket_manager_->getNodeID(cluster_id);
  EXPECT_EQ(result_for_cluster, node_id);

  // Call getNodeID with a node ID - should return the same node ID
  std::string result_for_node = socket_manager_->getNodeID(node_id);
  EXPECT_EQ(result_for_node, node_id);

  // Call getNodeID with a non-existent cluster ID - should return the key as-is
  // assuming it to be the node ID. A subsequent call to getConnectionSocket with
  // this node ID should return nullptr.
  const std::string non_existent_cluster = "non-existent-cluster";
  std::string result_for_non_existent = socket_manager_->getNodeID(non_existent_cluster);
  EXPECT_EQ(result_for_non_existent, non_existent_cluster);
}

TEST_F(TestUpstreamSocketManager, GetConnectionSocketEmpty) {
  // Test getting a socket when none exists
  auto socket = socket_manager_->getConnectionSocket("non-existent-node");
  EXPECT_EQ(socket, nullptr);
}

TEST_F(TestUpstreamSocketManager, CleanStaleNodeEntryWithActiveSockets) {
  // Test cleanStaleNodeEntry when node still has active sockets (should be no-op)
  auto socket1 = createMockSocket(123);
  auto socket2 = createMockSocket(456);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  // Add sockets and verify initial state
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket1), ping_interval,
                                       false);
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket2), ping_interval,
                                       false);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 2);
  EXPECT_EQ(getNodeToClusterMapping(node_id), cluster_id);
  EXPECT_EQ(getClusterToNodeMapping(cluster_id).size(), 1);

  // Call cleanStaleNodeEntry while sockets exist - should be no-op
  socket_manager_->cleanStaleNodeEntry(node_id);

  // Verify no cleanup happened (all mappings should remain unchanged)
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 2);
  EXPECT_EQ(getNodeToClusterMapping(node_id), cluster_id);
  EXPECT_EQ(getClusterToNodeMapping(cluster_id).size(), 1);
}

TEST_F(TestUpstreamSocketManager, CleanStaleNodeEntryClusterCleanup) {
  // Test that cluster entry is removed when last node is cleaned up
  auto socket1 = createMockSocket(123);
  auto socket2 = createMockSocket(456);
  const std::string node1 = "node1";
  const std::string node2 = "node2";
  const std::string cluster_id = "shared-cluster";
  const std::chrono::seconds ping_interval(30);

  // Add two nodes to the same cluster
  socket_manager_->addConnectionSocket(node1, cluster_id, std::move(socket1), ping_interval, false);
  socket_manager_->addConnectionSocket(node2, cluster_id, std::move(socket2), ping_interval, false);

  // Verify both nodes are in the cluster
  EXPECT_EQ(getNodeToClusterMapping(node1), cluster_id);
  EXPECT_EQ(getNodeToClusterMapping(node2), cluster_id);
  auto cluster_nodes = getClusterToNodeMapping(cluster_id);
  EXPECT_EQ(cluster_nodes.size(), 2);
  EXPECT_EQ(getClusterToNodeMapSize(), 1); // One cluster

  // Get socket from first node (should trigger cleanup for node1)
  auto retrieved_socket1 = socket_manager_->getConnectionSocket(node1);
  EXPECT_NE(retrieved_socket1, nullptr);

  // Verify node1 is cleaned up but cluster still exists for node2
  EXPECT_EQ(getNodeToClusterMapping(node1), "");         // node1 removed
  EXPECT_EQ(getNodeToClusterMapping(node2), cluster_id); // node2 still there
  cluster_nodes = getClusterToNodeMapping(cluster_id);
  EXPECT_EQ(cluster_nodes.size(), 1); // Only node2 remains
  EXPECT_EQ(cluster_nodes[0], node2);
  EXPECT_EQ(getClusterToNodeMapSize(), 1); // Cluster still exists

  // Get socket from second node (should trigger cleanup for node2 and remove cluster)
  auto retrieved_socket2 = socket_manager_->getConnectionSocket(node2);
  EXPECT_NE(retrieved_socket2, nullptr);

  // Verify both nodes and cluster are cleaned up
  EXPECT_EQ(getNodeToClusterMapping(node1), "");
  EXPECT_EQ(getNodeToClusterMapping(node2), "");
  cluster_nodes = getClusterToNodeMapping(cluster_id);
  EXPECT_EQ(cluster_nodes.size(), 0);      // No nodes in cluster
  EXPECT_EQ(getClusterToNodeMapSize(), 0); // Cluster completely removed
}

TEST_F(TestUpstreamSocketManager, FileEventAndTimerCleanup) {
  // Test that file events and timers are properly cleaned up when getting sockets
  auto socket1 = createMockSocket(123);
  auto socket2 = createMockSocket(456);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  // Add sockets
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket1), ping_interval,
                                       false);
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket2), ping_interval,
                                       false);

  // Verify file events and timers are created
  EXPECT_EQ(getFDToEventMapSize(), 2);
  EXPECT_EQ(getFDToTimerMapSize(), 2);

  // Get first socket - should clean up its file event and timer
  auto retrieved_socket1 = socket_manager_->getConnectionSocket(node_id);
  EXPECT_NE(retrieved_socket1, nullptr);

  // Verify that the entry for the fd is removed from the maps
  EXPECT_FALSE(verifyFDToEventMap(123));
  EXPECT_FALSE(verifyFDToTimerMap(123));

  // Get second socket - should clean up remaining file event and timer
  auto retrieved_socket2 = socket_manager_->getConnectionSocket(node_id);
  EXPECT_NE(retrieved_socket2, nullptr);

  // Verify all file events and timers are cleaned up
  EXPECT_EQ(getFDToEventMapSize(), 0);
  EXPECT_EQ(getFDToTimerMapSize(), 0);
}

// ============================================================================
// MarkSocketDead Tests
// ============================================================================

TEST_F(TestUpstreamSocketManager, MarkSocketNotPresentDead) {
  // Test MarkSocketDead with an fd which isn't in the fd_to_node_map_
  // Should log debug and return early
  socket_manager_->markSocketDead(999);

  // Test with negative fd
  socket_manager_->markSocketDead(-1);

  // Test with zero fd
  socket_manager_->markSocketDead(0);
}

TEST_F(TestUpstreamSocketManager, MarkIdleSocketDead) {
  // Test MarkSocketDead with an idle socket (in the pool)
  auto socket1 = createMockSocket(123);
  auto socket2 = createMockSocket(456);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  // Add sockets to the pool
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket1), ping_interval,
                                       false);
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket2), ping_interval,
                                       false);

  // Verify initial state
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 2);
  EXPECT_TRUE(verifyFDToNodeMap(123));

  // Mark first idle socket as dead
  socket_manager_->markSocketDead(123);

  // Verify markSocketDead touched the right maps:
  // 1. Socket removed from pool
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 1);
  // 2. FD mapping removed
  EXPECT_FALSE(verifyFDToNodeMap(123));
  // 3. File event and timer cleaned up for this specific FD
  EXPECT_FALSE(verifyFDToEventMap(123));
  EXPECT_FALSE(verifyFDToTimerMap(123));

  // Verify remaining socket is still accessible
  auto retrieved_socket = socket_manager_->getConnectionSocket(node_id);
  EXPECT_NE(retrieved_socket, nullptr);
}

TEST_F(TestUpstreamSocketManager, MarkUsedSocketDead) {
  // Test MarkSocketDead with a used socket
  auto socket = createMockSocket(123);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  // Add socket to pool
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval,
                                       false);

  // Verify socket is in pool
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 1);
  EXPECT_TRUE(verifyFDToNodeMap(123));

  // Get the socket (removes it from pool, simulating "used" state)
  auto retrieved_socket = socket_manager_->getConnectionSocket(node_id);
  EXPECT_NE(retrieved_socket, nullptr);

  // Verify socket is no longer in pool but FD mapping might still exist until cleanup
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 0);

  // Mark the used socket as dead - should only update stats and return
  socket_manager_->markSocketDead(123);

  // Verify FD mapping is removed
  EXPECT_FALSE(verifyFDToNodeMap(123));

  // Verify all mappings are cleaned up since no sockets remain
  EXPECT_EQ(getNodeToClusterMapping(node_id), "");
  auto cluster_nodes = getClusterToNodeMapping(cluster_id);
  EXPECT_EQ(cluster_nodes.size(), 0);
}

TEST_F(TestUpstreamSocketManager, MarkSocketDeadTriggerCleanup) {
  // Test that marking the last socket dead triggers cleanStaleNodeEntry
  auto socket = createMockSocket(123);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  // Add socket
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval,
                                       false);

  // Verify mappings exist
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 1);
  EXPECT_EQ(getNodeToClusterMapping(node_id), cluster_id);
  auto cluster_nodes = getClusterToNodeMapping(cluster_id);
  EXPECT_EQ(cluster_nodes.size(), 1);

  // Mark the socket as dead
  socket_manager_->markSocketDead(123);

  // Verify complete cleanup occurred
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 0);
  EXPECT_EQ(getNodeToClusterMapping(node_id), "");
  cluster_nodes = getClusterToNodeMapping(cluster_id);
  EXPECT_EQ(cluster_nodes.size(), 0);
  EXPECT_EQ(getAcceptedReverseConnectionsSize(), 0);
}

TEST_F(TestUpstreamSocketManager, MarkSocketDeadMultipleSockets) {
  // Test marking sockets dead when multiple exist for the same node
  auto socket1 = createMockSocket(123);
  auto socket2 = createMockSocket(456);
  auto socket3 = createMockSocket(789);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  // Add multiple sockets
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket1), ping_interval,
                                       false);
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket2), ping_interval,
                                       false);
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket3), ping_interval,
                                       false);

  // Verify all sockets are added
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 3);
  EXPECT_EQ(getFDToEventMapSize(), 3);
  EXPECT_EQ(getFDToTimerMapSize(), 3);

  // Mark first socket as dead
  socket_manager_->markSocketDead(123);

  // Verify specific socket removed, others remain
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 2);
  EXPECT_EQ(getFDToEventMapSize(), 2);
  EXPECT_EQ(getFDToTimerMapSize(), 2);
  // FD mapping removed
  EXPECT_FALSE(verifyFDToNodeMap(123));
  EXPECT_FALSE(verifyFDToEventMap(123));
  EXPECT_FALSE(verifyFDToTimerMap(123));
  // other socket still mapped
  EXPECT_TRUE(verifyFDToNodeMap(456));
  EXPECT_TRUE(verifyFDToNodeMap(789));

  // Node mappings should still exist
  EXPECT_EQ(getNodeToClusterMapping(node_id), cluster_id);

  // Mark second socket as dead
  socket_manager_->markSocketDead(456);

  // Verify specific socket removed
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 1);
  // FD mapping removed
  EXPECT_FALSE(verifyFDToNodeMap(456));
  EXPECT_FALSE(verifyFDToEventMap(456));
  EXPECT_FALSE(verifyFDToTimerMap(456));
  // other socket still mapped
  EXPECT_TRUE(verifyFDToNodeMap(789));

  // Mark last socket as dead - should trigger cleanup
  socket_manager_->markSocketDead(789);

  // Verify complete cleanup
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 0);
  EXPECT_EQ(getNodeToClusterMapping(node_id), "");
  EXPECT_EQ(getAcceptedReverseConnectionsSize(), 0);
  EXPECT_EQ(getFDToEventMapSize(), 0);
  EXPECT_EQ(getFDToTimerMapSize(), 0);
}

TEST_F(TestUpstreamSocketManager, PingConnectionsWriteSuccess) {
  // Test pingConnections when writing RPING succeeds
  auto socket1 = createMockSocket(123);
  auto socket2 = createMockSocket(456);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  // Add sockets first
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket1), ping_interval,
                                       false);
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket2), ping_interval,
                                       false);

  // Verify sockets are added
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 2);

  // Now get the IoHandles from the socket manager and set up mock expectations
  auto& sockets = getSocketsForNode(node_id);
  auto* mock_io_handle1 =
      dynamic_cast<NiceMock<Network::MockIoHandle>*>(&sockets.front()->ioHandle());
  auto* mock_io_handle2 =
      dynamic_cast<NiceMock<Network::MockIoHandle>*>(&sockets.back()->ioHandle());

  EXPECT_CALL(*mock_io_handle1, write(_))
      .WillRepeatedly(Invoke([](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        // Drain the buffer to simulate successful write
        buffer.drain(buffer.length());
        return Api::IoCallUint64Result{0, Network::IoSocketError::getIoSocketEagainError()};
      }));
  EXPECT_CALL(*mock_io_handle2, write(_))
      .WillRepeatedly(Invoke([](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        // Drain the buffer to simulate successful write
        buffer.drain(buffer.length());
        return Api::IoCallUint64Result{0, Network::IoSocketError::getIoSocketEagainError()};
      }));

  // Manually call pingConnections
  socket_manager_->pingConnections();

  // Verify sockets are still there (no cleanup occurred)
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 2);
}

TEST_F(TestUpstreamSocketManager, PingConnectionsWriteFailure) {
  // Test pingConnections when writing RPING fails - should trigger cleanup
  auto socket1 = createMockSocket(123);
  auto socket2 = createMockSocket(456);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  // Add sockets first (this will trigger pingConnections via tryEnablePingTimer)
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket1), ping_interval,
                                       false);
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket2), ping_interval,
                                       false);

  // Verify sockets are added
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 2);

  // Now get the IoHandles from the socket manager and set up mock expectations
  auto& sockets = getSocketsForNode(node_id);
  auto* mock_io_handle1 =
      dynamic_cast<NiceMock<Network::MockIoHandle>*>(&sockets.front()->ioHandle());
  auto* mock_io_handle2 =
      dynamic_cast<NiceMock<Network::MockIoHandle>*>(&sockets.back()->ioHandle());

  // First call: Send failed ping on mock_io_handle1
  // When the first socket fails, the loop breaks and doesn't process the second socket
  EXPECT_CALL(*mock_io_handle1, write(_))
      .Times(1) // Called once
      .WillRepeatedly(Invoke([](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        // Drain the buffer to simulate write attempt
        buffer.drain(buffer.length());
        return Api::IoCallUint64Result{0, Network::IoSocketError::create(ECONNRESET)};
      }));
  // Second socket should NOT be called in the first pingConnections call

  // Manually call pingConnections to test the functionality
  socket_manager_->pingConnections(node_id);

  // Verify first socket was cleaned up but second socket remains (node not cleaned up)
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 1); // Second socket still there
  EXPECT_FALSE(verifyFDToNodeMap(123));                       // First socket removed
  EXPECT_TRUE(verifyFDToNodeMap(456));                        // Second socket still there
  EXPECT_EQ(getNodeToClusterMapping(node_id), cluster_id);    // Node mapping still exists
  EXPECT_EQ(getAcceptedReverseConnectionsSize(), 1);          // One node still exists

  // Now send failed ping on mock_io_handle2 to trigger ping failure and node cleanup
  EXPECT_CALL(*mock_io_handle2, write(_))
      .Times(1) // Called once during second ping
      .WillRepeatedly(Invoke([](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        // Drain the buffer to simulate write attempt
        buffer.drain(buffer.length());
        return Api::IoCallUint64Result{0, Network::IoSocketError::create(EPIPE)};
      }));

  // Manually call pingConnections again. This should ping socket2, fail and trigger node cleanup
  socket_manager_->pingConnections(node_id);

  // Verify complete cleanup occurred (both sockets removed due to node cleanup)
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 0);
  EXPECT_FALSE(verifyFDToNodeMap(123));
  EXPECT_FALSE(verifyFDToNodeMap(456));
  EXPECT_EQ(getNodeToClusterMapping(node_id), "");
  EXPECT_EQ(getAcceptedReverseConnectionsSize(), 0);
}

TEST_F(TestUpstreamSocketManager, OnPingResponseValidResponse) {
  // Test onPingResponse with valid ping response
  auto socket = createMockSocket(123);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  // Add socket
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval,
                                       false);

  // Create mock IoHandle for ping response
  auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
  EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(Return(123));

  // Mock successful read with valid ping response
  const std::string ping_response = "RPING";
  EXPECT_CALL(*mock_io_handle, read(_, _))
      .WillOnce([&](Buffer::Instance& buffer, absl::optional<uint64_t>) -> Api::IoCallUint64Result {
        buffer.add(ping_response);
        return Api::IoCallUint64Result{ping_response.size(), Api::IoError::none()};
      });

  // Call onPingResponse - should succeed and not mark socket dead
  socket_manager_->onPingResponse(*mock_io_handle);

  // Socket should still be alive
  EXPECT_TRUE(verifyFDToNodeMap(123));
}

TEST_F(TestUpstreamSocketManager, OnPingResponseReadError) {
  // Test onPingResponse with read error
  auto socket = createMockSocket(123);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  // Add socket
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval,
                                       false);

  // Create mock IoHandle for ping response
  auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
  EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(Return(123));

  // Mock read error
  EXPECT_CALL(*mock_io_handle, read(_, _))
      .WillOnce(
          Return(Api::IoCallUint64Result{0, Network::IoSocketError::getIoSocketEagainError()}));

  // Call onPingResponse - should mark socket dead due to read error
  socket_manager_->onPingResponse(*mock_io_handle);

  // Socket should be marked dead and removed
  EXPECT_FALSE(verifyFDToNodeMap(123));
}

TEST_F(TestUpstreamSocketManager, OnPingResponseConnectionClosed) {
  // Test onPingResponse when connection is closed (0 bytes read)
  auto socket = createMockSocket(123);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  // Add socket
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval,
                                       false);

  // Create mock IoHandle for ping response
  auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
  EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(Return(123));

  // Mock connection closed (0 bytes read)
  EXPECT_CALL(*mock_io_handle, read(_, _))
      .WillOnce(Return(Api::IoCallUint64Result{0, Api::IoError::none()}));

  // Call onPingResponse - should mark socket dead due to connection closed
  socket_manager_->onPingResponse(*mock_io_handle);

  // Socket should be marked dead and removed
  EXPECT_FALSE(verifyFDToNodeMap(123));
}

TEST_F(TestUpstreamSocketManager, OnPingResponseInvalidData) {
  // Test onPingResponse with invalid ping response data
  auto socket = createMockSocket(123);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  // Add socket
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval,
                                       false);

  // Create mock IoHandle for ping response
  auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
  EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(Return(123));

  // Mock successful read but with invalid ping response
  const std::string invalid_response = "INVALID_DATA";
  EXPECT_CALL(*mock_io_handle, read(_, _))
      .WillOnce([&](Buffer::Instance& buffer, absl::optional<uint64_t>) -> Api::IoCallUint64Result {
        buffer.add(invalid_response);
        return Api::IoCallUint64Result{invalid_response.size(), Api::IoError::none()};
      });

  // Call onPingResponse - should mark socket dead due to invalid response
  socket_manager_->onPingResponse(*mock_io_handle);

  // Socket should be marked dead and removed
  EXPECT_FALSE(verifyFDToNodeMap(123));
}

class TestReverseTunnelAcceptor : public testing::Test {
protected:
  TestReverseTunnelAcceptor() {
    // Set up the stats scope
    stats_scope_ = Stats::ScopeSharedPtr(stats_store_.createScope("test_scope."));

    // Set up the mock context
    EXPECT_CALL(context_, threadLocal()).WillRepeatedly(ReturnRef(thread_local_));
    EXPECT_CALL(context_, scope()).WillRepeatedly(ReturnRef(*stats_scope_));

    // Create the config
    config_.set_stat_prefix("test_prefix");

    // Create the socket interface
    socket_interface_ = std::make_unique<ReverseTunnelAcceptor>(context_);

    // Create the extension
    extension_ =
        std::make_unique<ReverseTunnelAcceptorExtension>(*socket_interface_, context_, config_);

    // Set up mock dispatcher with default expectations
    EXPECT_CALL(dispatcher_, createTimer_(_))
        .WillRepeatedly(testing::ReturnNew<NiceMock<Event::MockTimer>>());
    EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, _))
        .WillRepeatedly(testing::ReturnNew<NiceMock<Event::MockFileEvent>>());

    // Create the socket manager with real extension
    socket_manager_ = std::make_unique<UpstreamSocketManager>(dispatcher_, extension_.get());
  }

  void TearDown() override {
    // Destroy socket manager first so it can still access thread local slot during cleanup
    socket_manager_.reset();

    // Then destroy thread local components
    tls_slot_.reset();
    thread_local_registry_.reset();

    extension_.reset();
    socket_interface_.reset();
  }

  // Helper to set up thread local slot for tests
  void setupThreadLocalSlot() {
    // First, call onServerInitialized to set up the extension reference properly
    extension_->onServerInitialized();

    // Create a thread local registry with the properly initialized extension
    thread_local_registry_ =
        std::make_shared<UpstreamSocketThreadLocal>(dispatcher_, extension_.get());

    // Create the actual TypedSlot
    tls_slot_ = ThreadLocal::TypedSlot<UpstreamSocketThreadLocal>::makeUnique(thread_local_);
    thread_local_.setDispatcher(&dispatcher_);

    // Set up the slot to return our registry
    tls_slot_->set([registry = thread_local_registry_](Event::Dispatcher&) { return registry; });

    // Override the TLS slot with our test version
    extension_->setTestOnlyTLSRegistry(std::move(tls_slot_));
  }

  // Helper to create a mock socket with proper address setup
  Network::ConnectionSocketPtr createMockSocket(int fd = 123,
                                                const std::string& local_addr = "127.0.0.1:8080",
                                                const std::string& remote_addr = "127.0.0.1:9090") {
    auto socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();

    // Parse local address (IP:port format)
    auto local_colon_pos = local_addr.find(':');
    std::string local_ip = local_addr.substr(0, local_colon_pos);
    uint32_t local_port = std::stoi(local_addr.substr(local_colon_pos + 1));
    auto local_address = Network::Utility::parseInternetAddressNoThrow(local_ip, local_port);

    // Parse remote address (IP:port format)
    auto remote_colon_pos = remote_addr.find(':');
    std::string remote_ip = remote_addr.substr(0, remote_colon_pos);
    uint32_t remote_port = std::stoi(remote_addr.substr(remote_colon_pos + 1));
    auto remote_address = Network::Utility::parseInternetAddressNoThrow(remote_ip, remote_port);

    // Create a mock IO handle and set it up
    auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
    auto* mock_io_handle_ptr = mock_io_handle.get();
    EXPECT_CALL(*mock_io_handle_ptr, fdDoNotUse()).WillRepeatedly(Return(fd));
    EXPECT_CALL(*socket, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle_ptr));

    // Store the mock_io_handle in the socket
    socket->io_handle_ = std::move(mock_io_handle);

    // Set up connection info provider with the desired addresses
    socket->connection_info_provider_->setLocalAddress(local_address);
    socket->connection_info_provider_->setRemoteAddress(remote_address);

    return socket;
  }

  // Helper to create an address with a specific logical name for testing. This allows us to test
  // reverse connection address socket creation.
  Network::Address::InstanceConstSharedPtr
  createAddressWithLogicalName(const std::string& logical_name) {
    // Create a simple address that returns the specified logical name
    class TestAddress : public Network::Address::Instance {
    public:
      TestAddress(const std::string& logical_name) : logical_name_(logical_name) {
        address_string_ = "127.0.0.1:8080"; // Dummy address string
      }

      bool operator==(const Instance& rhs) const override {
        return logical_name_ == rhs.logicalName();
      }
      Network::Address::Type type() const override { return Network::Address::Type::Ip; }
      const std::string& asString() const override { return address_string_; }
      absl::string_view asStringView() const override { return address_string_; }
      const std::string& logicalName() const override { return logical_name_; }
      const Network::Address::Ip* ip() const override { return nullptr; }
      const Network::Address::Pipe* pipe() const override { return nullptr; }
      const Network::Address::EnvoyInternalAddress* envoyInternalAddress() const override {
        return nullptr;
      }
      const sockaddr* sockAddr() const override { return nullptr; }
      socklen_t sockAddrLen() const override { return 0; }
      absl::string_view addressType() const override { return "test"; }
      absl::optional<std::string> networkNamespace() const override { return absl::nullopt; }
      const Network::SocketInterface& socketInterface() const override {
        return Network::SocketInterfaceSingleton::get();
      }

    private:
      std::string logical_name_;
      std::string address_string_;
    };

    return std::make_shared<TestAddress>(logical_name);
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<ThreadLocal::MockInstance> thread_local_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopeSharedPtr stats_scope_;
  NiceMock<Event::MockDispatcher> dispatcher_;

  envoy::extensions::bootstrap::reverse_connection_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface config_;

  std::unique_ptr<ReverseTunnelAcceptor> socket_interface_;
  std::unique_ptr<ReverseTunnelAcceptorExtension> extension_;
  std::unique_ptr<UpstreamSocketManager> socket_manager_;

  // Real thread local slot and registry
  std::unique_ptr<ThreadLocal::TypedSlot<UpstreamSocketThreadLocal>> tls_slot_;
  std::shared_ptr<UpstreamSocketThreadLocal> thread_local_registry_;
};

TEST_F(TestReverseTunnelAcceptor, GetLocalRegistryNoExtension) {
  // Test getLocalRegistry when extension is not set
  auto* registry = socket_interface_->getLocalRegistry();
  EXPECT_EQ(registry, nullptr);
}

TEST_F(TestReverseTunnelAcceptor, GetLocalRegistryWithExtension) {
  // Test getLocalRegistry when extension is set
  setupThreadLocalSlot();

  auto* registry = socket_interface_->getLocalRegistry();
  EXPECT_NE(registry, nullptr);
  EXPECT_EQ(registry, thread_local_registry_.get());
}

TEST_F(TestReverseTunnelAcceptor, CreateBootstrapExtension) {
  // Test createBootstrapExtension function
  auto extension = socket_interface_->createBootstrapExtension(config_, context_);
  EXPECT_NE(extension, nullptr);
}

TEST_F(TestReverseTunnelAcceptor, CreateEmptyConfigProto) {
  // Test createEmptyConfigProto function
  auto config = socket_interface_->createEmptyConfigProto();
  EXPECT_NE(config, nullptr);
}

TEST_F(TestReverseTunnelAcceptor, SocketWithoutAddress) {
  // Test socket() without address - should return nullptr
  Network::SocketCreationOptions options;
  auto io_handle =
      socket_interface_->socket(Network::Socket::Type::Stream, Network::Address::Type::Ip,
                                Network::Address::IpVersion::v4, false, options);
  EXPECT_EQ(io_handle, nullptr);
}

TEST_F(TestReverseTunnelAcceptor, SocketWithAddressNoThreadLocal) {
  // Test socket() with reverse connection address but no thread local slot initialized - should
  // fall back to default socket interface Do not setup thread local slot
  const std::string node_id = "test-node";
  auto address = createAddressWithLogicalName(node_id);
  Network::SocketCreationOptions options;
  auto io_handle = socket_interface_->socket(Network::Socket::Type::Stream, address, options);
  EXPECT_NE(io_handle, nullptr); // Should return default socket interface

  // Verify that the io_handle is a default IoHandle, not an UpstreamReverseConnectionIOHandle
  EXPECT_EQ(dynamic_cast<UpstreamReverseConnectionIOHandle*>(io_handle.get()), nullptr);

  // Verify fallback counter increments for diagnostics.
  // Counter name is "<scope>.<stat_prefix>.fallback_no_reverse_socket".
  auto& scope = extension_->getStatsScope();
  auto& counter = scope.counterFromString(
      absl::StrCat(extension_->statPrefix(), ".fallback_no_reverse_socket"));
  EXPECT_EQ(counter.value(), 1);
}

TEST_F(TestReverseTunnelAcceptor, SocketWithAddressAndThreadLocalNoCachedSockets) {
  // Test socket() with reverse connection address and thread local slot but no cached sockets -
  // should fall back to default socket interface
  setupThreadLocalSlot();

  const std::string node_id = "test-node";
  auto address = createAddressWithLogicalName(node_id);

  // Call socket() before calling addConnectionSocket() so that no sockets are cached
  Network::SocketCreationOptions options;
  auto io_handle = socket_interface_->socket(Network::Socket::Type::Stream, address, options);
  EXPECT_NE(io_handle, nullptr); // Should fall back to default socket interface

  // Verify that the io_handle is a default IoHandle, not an UpstreamReverseConnectionIOHandle
  EXPECT_EQ(dynamic_cast<UpstreamReverseConnectionIOHandle*>(io_handle.get()), nullptr);
}

TEST_F(TestReverseTunnelAcceptor, SocketWithAddressAndThreadLocalWithCachedSockets) {
  // Test socket() with address and thread local slot with cached sockets
  setupThreadLocalSlot();

  // Get the socket manager from the thread local registry
  auto* tls_socket_manager = socket_interface_->getLocalRegistry()->socketManager();
  EXPECT_NE(tls_socket_manager, nullptr);

  // Add a socket to the thread local socket manager
  auto socket = createMockSocket(123);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  tls_socket_manager->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval,
                                          false);

  // Create address with the same logical name as the node_id
  auto address = createAddressWithLogicalName(node_id);

  Network::SocketCreationOptions options;
  auto io_handle = socket_interface_->socket(Network::Socket::Type::Stream, address, options);
  EXPECT_NE(io_handle, nullptr); // Should return cached socket

  // Verify that we got an UpstreamReverseConnectionIOHandle
  auto* upstream_io_handle = dynamic_cast<UpstreamReverseConnectionIOHandle*>(io_handle.get());
  EXPECT_NE(upstream_io_handle, nullptr);

  // Try to get another socket for the same node. This will return a default IoHandle, not an
  // UpstreamReverseConnectionIOHandle
  auto another_io_handle =
      socket_interface_->socket(Network::Socket::Type::Stream, address, options);
  EXPECT_NE(another_io_handle, nullptr);
  // This should be a default IoHandle, not an UpstreamReverseConnectionIOHandle
  EXPECT_EQ(dynamic_cast<UpstreamReverseConnectionIOHandle*>(another_io_handle.get()), nullptr);
}

TEST_F(TestReverseTunnelAcceptor, IpFamilySupported) {
  // Reverse connection sockets support standard IP families. (IPv4 and IPv6)
  EXPECT_TRUE(socket_interface_->ipFamilySupported(AF_INET));
  EXPECT_TRUE(socket_interface_->ipFamilySupported(AF_INET6));
  EXPECT_FALSE(socket_interface_->ipFamilySupported(AF_UNIX));
}

class TestUpstreamReverseConnectionIOHandle : public testing::Test {
protected:
  TestUpstreamReverseConnectionIOHandle() {
    // Create a mock socket for testing
    mock_socket_ = std::make_unique<NiceMock<Network::MockConnectionSocket>>();

    // Create a mock IO handle
    auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
    EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(Return(123));
    EXPECT_CALL(*mock_socket_, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle));

    // Store the mock IO handle in the socket
    mock_socket_->io_handle_ = std::move(mock_io_handle);

    // Create the IO handle under test
    io_handle_ = std::make_unique<UpstreamReverseConnectionIOHandle>(std::move(mock_socket_),
                                                                     "test-cluster");
  }

  void TearDown() override { io_handle_.reset(); }

  std::unique_ptr<NiceMock<Network::MockConnectionSocket>> mock_socket_;
  std::unique_ptr<UpstreamReverseConnectionIOHandle> io_handle_;
};

TEST_F(TestUpstreamReverseConnectionIOHandle, ConnectReturnsSuccess) {
  // Test that connect() returns success immediately for reverse connections
  auto address = Network::Utility::parseInternetAddressNoThrow("127.0.0.1", 8080);

  // For UpstreamReverseConnectionIOHandle, connect() is a no-op.
  auto result = io_handle_->connect(address);

  // Should return success (0) with no error
  EXPECT_EQ(result.return_value_, 0);
  EXPECT_EQ(result.errno_, 0);
}

TEST_F(TestUpstreamReverseConnectionIOHandle, CloseCleansUpSocket) {
  // Test that close() properly cleans up the owned socket
  auto result = io_handle_->close();

  // Should successfully close the socket and return
  EXPECT_EQ(result.err_, nullptr);
}

TEST_F(TestUpstreamReverseConnectionIOHandle, GetSocketReturnsConstReference) {
  // Test that getSocket() returns a const reference to the owned socket
  const auto& socket = io_handle_->getSocket();

  // Should return a valid reference
  EXPECT_NE(&socket, nullptr);
}

TEST_F(ReverseTunnelAcceptorExtensionTest, IpFamilySupportIPv4) {
  // Test that IPv4 is supported
  EXPECT_TRUE(socket_interface_->ipFamilySupported(AF_INET));
}

TEST_F(ReverseTunnelAcceptorExtensionTest, IpFamilySupportIPv6) {
  // Test that IPv6 is supported
  EXPECT_TRUE(socket_interface_->ipFamilySupported(AF_INET6));
}

TEST_F(ReverseTunnelAcceptorExtensionTest, IpFamilySupportUnknown) {
  // Test that unknown families are not supported
  EXPECT_FALSE(socket_interface_->ipFamilySupported(AF_UNIX));
  EXPECT_FALSE(socket_interface_->ipFamilySupported(-1));
}

TEST_F(ReverseTunnelAcceptorExtensionTest, ExtensionNotInitialized) {
  // Test that we handle calls before onServerInitialized
  ReverseTunnelAcceptor acceptor(context_);

  auto registry = acceptor.getLocalRegistry();
  EXPECT_EQ(registry, nullptr);
}

TEST_F(ReverseTunnelAcceptorExtensionTest, CreateEmptyConfigProto) {
  // Test that createEmptyConfigProto returns valid proto
  auto proto = socket_interface_->createEmptyConfigProto();
  EXPECT_NE(proto, nullptr);

  // Should be able to cast to the correct type
  auto* typed_proto =
      dynamic_cast<envoy::extensions::bootstrap::reverse_connection_socket_interface::v3::
                       UpstreamReverseConnectionSocketInterface*>(proto.get());
  EXPECT_NE(typed_proto, nullptr);
}

TEST_F(ReverseTunnelAcceptorExtensionTest, FactoryName) {
  // Test that factory returns correct name
  EXPECT_EQ(socket_interface_->name(),
            "envoy.bootstrap.reverse_connection.upstream_reverse_connection_socket_interface");
}

class UpstreamReverseConnectionIOHandleTest : public testing::Test {
protected:
  void SetUp() override {
    auto socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();

    auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
    EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(Return(123));
    EXPECT_CALL(*socket, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle));

    socket->io_handle_ = std::move(mock_io_handle);

    handle_ =
        std::make_unique<UpstreamReverseConnectionIOHandle>(std::move(socket), "test-cluster");
  }

  std::unique_ptr<UpstreamReverseConnectionIOHandle> handle_;
};

TEST_F(UpstreamReverseConnectionIOHandleTest, ConnectReturnsSuccess) {
  // Test that connect() returns success immediately for reverse connections
  auto address = Network::Utility::parseInternetAddressNoThrow("127.0.0.1", 8080);

  auto result = handle_->connect(address);

  EXPECT_EQ(result.return_value_, 0);
  EXPECT_EQ(result.errno_, 0);
}

TEST_F(UpstreamReverseConnectionIOHandleTest, GetSocketReturnsValidReference) {
  // Test that getSocket() returns a valid reference
  const auto& socket = handle_->getSocket();
  EXPECT_NE(&socket, nullptr);
}

// Configuration validation tests
class ConfigValidationTest : public testing::Test {
protected:
  envoy::extensions::bootstrap::reverse_connection_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface config_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
};

TEST_F(ConfigValidationTest, ValidConfiguration) {
  // Test that valid configuration gets accepted
  config_.set_stat_prefix("reverse_tunnel");

  ReverseTunnelAcceptor acceptor(context_);

  // Should not throw when creating bootstrap extension
  EXPECT_NO_THROW(acceptor.createBootstrapExtension(config_, context_));
}

TEST_F(ConfigValidationTest, EmptyStatPrefix) {
  // Test that empty stat_prefix still works with default
  ReverseTunnelAcceptor acceptor(context_);

  // Should not throw and should use default prefix
  EXPECT_NO_THROW(acceptor.createBootstrapExtension(config_, context_));
}

TEST_F(TestUpstreamSocketManager, GetConnectionSocketNoSocketsButValidMapping) {
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";

  // Manually add mapping without adding any actual sockets
  addNodeToClusterMapping(node_id, cluster_id);

  // Try to get a socket - should hit the "No available sockets" log and return nullptr
  auto socket = socket_manager_->getConnectionSocket(node_id);
  EXPECT_EQ(socket, nullptr);
}

TEST_F(TestUpstreamSocketManager, MarkSocketDeadInvalidSocketNotInPool) {
  auto socket = createMockSocket(123);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  // Add socket to create mappings
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval,
                                       false);

  // Get the socket (removes it from pool but keeps fd mapping temporarily)
  auto retrieved_socket = socket_manager_->getConnectionSocket(node_id);
  EXPECT_NE(retrieved_socket, nullptr);

  // Manually add the fd back to fd_to_node_map to simulate the edge case
  addFDToNodeMapping(123, node_id);

  // Now mark socket dead - it should find the node but not find the socket in the pool
  // This will trigger the "Marking an invalid socket dead" error log
  socket_manager_->markSocketDead(123);

  // Verify the fd mapping was cleaned up
  EXPECT_FALSE(verifyFDToNodeMap(123));
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
