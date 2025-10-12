#include "envoy/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/v3/upstream_reverse_connection_socket_interface.pb.h"

#include "source/common/network/utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor_extension.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/upstream_socket_manager.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class TestUpstreamSocketManager : public testing::Test {
protected:
  TestUpstreamSocketManager() {
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

    // Set up mock dispatcher with default expectations.
    EXPECT_CALL(dispatcher_, createTimer_(_))
        .WillRepeatedly(testing::ReturnNew<NiceMock<Event::MockTimer>>());
    EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, _))
        .WillRepeatedly(testing::ReturnNew<NiceMock<Event::MockFileEvent>>());

    // Create the socket manager with real extension.
    socket_manager_ = std::make_unique<UpstreamSocketManager>(dispatcher_, extension_.get());
  }

  void TearDown() override {
    socket_manager_.reset();
    extension_.reset();
    socket_interface_.reset();
  }

  // Helper methods to access private members (friend class works for these methods).
  void verifyInitialState() {
    EXPECT_EQ(socket_manager_->accepted_reverse_connections_.size(), 0);
    EXPECT_EQ(socket_manager_->fd_to_node_map_.size(), 0);
    EXPECT_EQ(socket_manager_->node_to_cluster_map_.size(), 0);
    EXPECT_EQ(socket_manager_->cluster_to_node_map_.size(), 0);
  }

  bool verifyFDToNodeMap(int fd) {
    return socket_manager_->fd_to_node_map_.find(fd) != socket_manager_->fd_to_node_map_.end();
  }

  bool verifyFDToClusterMap(int fd) {
    return socket_manager_->fd_to_cluster_map_.find(fd) !=
           socket_manager_->fd_to_cluster_map_.end();
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

  // Helper methods for the new test cases.
  void addNodeToClusterMapping(const std::string& node_id, const std::string& cluster_id) {
    socket_manager_->node_to_cluster_map_[node_id] = cluster_id;
    socket_manager_->cluster_to_node_map_[cluster_id].push_back(node_id);
  }

  void addFDToNodeMapping(int fd, const std::string& node_id) {
    socket_manager_->fd_to_node_map_[fd] = node_id;
  }

  // Helper to create a mock socket with proper address setup.
  Network::ConnectionSocketPtr createMockSocket(int fd = 123,
                                                const std::string& local_addr = "127.0.0.1:8080",
                                                const std::string& remote_addr = "127.0.0.1:9090") {
    auto socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();

    // Parse local address (IP:port format).
    auto local_colon_pos = local_addr.find(':');
    std::string local_ip = local_addr.substr(0, local_colon_pos);
    uint32_t local_port = std::stoi(local_addr.substr(local_colon_pos + 1));
    auto local_address = Network::Utility::parseInternetAddressNoThrow(local_ip, local_port);

    // Parse remote address (IP:port format).
    auto remote_colon_pos = remote_addr.find(':');
    std::string remote_ip = remote_addr.substr(0, remote_colon_pos);
    uint32_t remote_port = std::stoi(remote_addr.substr(remote_colon_pos + 1));
    auto remote_address = Network::Utility::parseInternetAddressNoThrow(remote_ip, remote_port);

    // Create a mock IO handle and set it up.
    auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
    auto* mock_io_handle_ptr = mock_io_handle.get();
    EXPECT_CALL(*mock_io_handle_ptr, fdDoNotUse()).WillRepeatedly(Return(fd));
    EXPECT_CALL(*socket, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle_ptr));

    // Store the mock_io_handle in the socket.
    socket->io_handle_ = std::move(mock_io_handle);

    // Set up connection info provider with the desired addresses.
    socket->connection_info_provider_->setLocalAddress(local_address);
    socket->connection_info_provider_->setRemoteAddress(remote_address);

    return socket;
  }

  // Helper to get sockets for a node.
  std::list<Network::ConnectionSocketPtr>& getSocketsForNode(const std::string& node_id) {
    return socket_manager_->accepted_reverse_connections_[node_id];
  }

  // Helper to manipulate node connection count for rebalancing tests.
  void setNodeConnCount(UpstreamSocketManager* manager, const std::string& node_id, int count) {
    manager->node_to_conn_count_map_[node_id] = count;
  }

  int getNodeConnCount(UpstreamSocketManager* manager, const std::string& node_id) {
    auto it = manager->node_to_conn_count_map_.find(node_id);
    return (it != manager->node_to_conn_count_map_.end()) ? it->second : 0;
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<ThreadLocal::MockInstance> thread_local_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopeSharedPtr stats_scope_;
  NiceMock<Event::MockDispatcher> dispatcher_;

  envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface config_;

  std::unique_ptr<ReverseTunnelAcceptor> socket_interface_;
  std::unique_ptr<ReverseTunnelAcceptorExtension> extension_;
  std::unique_ptr<UpstreamSocketManager> socket_manager_;

  // Set log level to debug for this test class.
  LogLevelSetter log_level_setter_ = LogLevelSetter(spdlog::level::debug);
};

TEST_F(TestUpstreamSocketManager, CreateUpstreamSocketManager) {
  EXPECT_NE(socket_manager_, nullptr);
  auto socket_manager_no_extension = std::make_unique<UpstreamSocketManager>(dispatcher_, nullptr);
  EXPECT_NE(socket_manager_no_extension, nullptr);
}

TEST_F(TestUpstreamSocketManager, GetUpstreamExtension) {
  EXPECT_EQ(socket_manager_->getUpstreamExtension(), extension_.get());
  auto socket_manager_no_extension = std::make_unique<UpstreamSocketManager>(dispatcher_, nullptr);
  EXPECT_EQ(socket_manager_no_extension->getUpstreamExtension(), nullptr);
}

TEST_F(TestUpstreamSocketManager, AddConnectionSocketEmptyClusterId) {
  auto socket = createMockSocket(123);
  const std::string node_id = "test-node";
  const std::string cluster_id = "";
  const std::chrono::seconds ping_interval(30);

  verifyInitialState();
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval);
  verifyInitialState();
  EXPECT_EQ(getFDToEventMapSize(), 0);
  EXPECT_EQ(getFDToTimerMapSize(), 0);

  auto retrieved_socket = socket_manager_->getConnectionSocket(node_id);
  EXPECT_EQ(retrieved_socket, nullptr);
}

TEST_F(TestUpstreamSocketManager, AddConnectionSocketEmptyNodeId) {
  auto socket = createMockSocket(456);
  const std::string node_id = "";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  verifyInitialState();
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval);
  verifyInitialState();
  EXPECT_EQ(getFDToEventMapSize(), 0);
  EXPECT_EQ(getFDToTimerMapSize(), 0);

  auto retrieved_socket = socket_manager_->getConnectionSocket(node_id);
  EXPECT_EQ(retrieved_socket, nullptr);
}

TEST_F(TestUpstreamSocketManager, AddAndGetMultipleSocketsSameNode) {
  auto socket1 = createMockSocket(123);
  auto socket2 = createMockSocket(456);
  auto socket3 = createMockSocket(789);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  verifyInitialState();

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket1), ping_interval);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 1);
  EXPECT_TRUE(verifyFDToNodeMap(123));
  EXPECT_TRUE(verifyFDToClusterMap(123));
  EXPECT_EQ(getNodeToClusterMapping(node_id), cluster_id);

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket2), ping_interval);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 2);
  EXPECT_TRUE(verifyFDToNodeMap(456));
  EXPECT_TRUE(verifyFDToClusterMap(456));

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket3), ping_interval);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 3);
  EXPECT_TRUE(verifyFDToNodeMap(789));
  EXPECT_TRUE(verifyFDToClusterMap(789));

  EXPECT_EQ(getFDToEventMapSize(), 3);
  EXPECT_EQ(getFDToTimerMapSize(), 3);

  auto retrieved_socket1 = socket_manager_->getConnectionSocket(node_id);
  EXPECT_NE(retrieved_socket1, nullptr);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 2);

  auto retrieved_socket2 = socket_manager_->getConnectionSocket(node_id);
  EXPECT_NE(retrieved_socket2, nullptr);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 1);

  auto retrieved_socket3 = socket_manager_->getConnectionSocket(node_id);
  EXPECT_NE(retrieved_socket3, nullptr);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 0);

  auto retrieved_socket4 = socket_manager_->getConnectionSocket(node_id);
  EXPECT_EQ(retrieved_socket4, nullptr);
}

TEST_F(TestUpstreamSocketManager, AddAndGetSocketsMultipleNodes) {
  auto socket1 = createMockSocket(123);
  auto socket2 = createMockSocket(456);
  const std::string node1 = "node1";
  const std::string node2 = "node2";
  const std::string cluster1 = "cluster1";
  const std::string cluster2 = "cluster2";
  const std::chrono::seconds ping_interval(30);

  verifyInitialState();

  socket_manager_->addConnectionSocket(node1, cluster1, std::move(socket1), ping_interval);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node1), 1);
  EXPECT_EQ(getNodeToClusterMapping(node1), cluster1);

  socket_manager_->addConnectionSocket(node2, cluster2, std::move(socket2), ping_interval);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node1), 1);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node2), 1);
  EXPECT_EQ(getNodeToClusterMapping(node1), cluster1);
  EXPECT_EQ(getNodeToClusterMapping(node2), cluster2);

  EXPECT_EQ(getFDToEventMapSize(), 2);
  EXPECT_EQ(getFDToTimerMapSize(), 2);

  auto retrieved_socket1 = socket_manager_->getConnectionSocket(node1);
  EXPECT_NE(retrieved_socket1, nullptr);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node1), 0);

  auto retrieved_socket2 = socket_manager_->getConnectionSocket(node2);
  EXPECT_NE(retrieved_socket2, nullptr);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node2), 0);
}

TEST_F(TestUpstreamSocketManager, TestGetNodeID) {
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  auto socket1 = createMockSocket(123);
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket1), ping_interval);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 1);
  EXPECT_EQ(getNodeToClusterMapping(node_id), cluster_id);

  // Key is a cluster ID with idle connections. This should return the node ID.
  std::string result_for_cluster = socket_manager_->getNodeID(cluster_id);
  EXPECT_EQ(result_for_cluster, node_id);

  // Key is a node ID. This should return it as-is.
  std::string result_for_node = socket_manager_->getNodeID(node_id);
  EXPECT_EQ(result_for_node, node_id);

  // Key doesn't exist. This should return it as-is.
  const std::string non_existent_cluster = "non-existent-cluster";
  std::string result_for_non_existent = socket_manager_->getNodeID(non_existent_cluster);
  EXPECT_EQ(result_for_non_existent, non_existent_cluster);

  // Key is a cluster ID but no nodes have idle connections.
  // Retrieve the only socket, making the node have no idle connections.
  auto retrieved_socket = socket_manager_->getConnectionSocket(node_id);
  EXPECT_NE(retrieved_socket, nullptr);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 0);

  // getNodeID with cluster_id should return cluster_id as-is since no nodes have idle connections.
  std::string result_for_cluster_no_idle = socket_manager_->getNodeID(cluster_id);
  EXPECT_EQ(result_for_cluster_no_idle, cluster_id);
}

TEST_F(TestUpstreamSocketManager, GetConnectionSocketEmpty) {
  auto socket = socket_manager_->getConnectionSocket("non-existent-node");
  EXPECT_EQ(socket, nullptr);
}

TEST_F(TestUpstreamSocketManager, CleanStaleNodeEntryWithActiveSockets) {
  auto socket1 = createMockSocket(123);
  auto socket2 = createMockSocket(456);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket1), ping_interval);
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket2), ping_interval);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 2);
  EXPECT_EQ(getNodeToClusterMapping(node_id), cluster_id);
  EXPECT_EQ(getClusterToNodeMapping(cluster_id).size(), 1);

  socket_manager_->cleanStaleNodeEntry(node_id);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 2);
  EXPECT_EQ(getNodeToClusterMapping(node_id), cluster_id);
  EXPECT_EQ(getClusterToNodeMapping(cluster_id).size(), 1);
}

TEST_F(TestUpstreamSocketManager, CleanStaleNodeEntryClusterCleanup) {
  auto socket1 = createMockSocket(123);
  auto socket2 = createMockSocket(456);
  auto socket3 = createMockSocket(789);
  const std::string node1 = "node1";
  const std::string node2 = "node2";
  const std::string cluster_id = "shared-cluster";
  const std::chrono::seconds ping_interval(30);

  socket_manager_->addConnectionSocket(node1, cluster_id, std::move(socket1), ping_interval);
  // Add two sockets for node2 to test the cleanStaleNodeEntry logic.
  socket_manager_->addConnectionSocket(node2, cluster_id, std::move(socket2), ping_interval);
  socket_manager_->addConnectionSocket(node2, cluster_id, std::move(socket3), ping_interval);

  EXPECT_EQ(getNodeToClusterMapping(node1), cluster_id);
  EXPECT_EQ(getNodeToClusterMapping(node2), cluster_id);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node2), 2);
  auto cluster_nodes = getClusterToNodeMapping(cluster_id);
  EXPECT_EQ(cluster_nodes.size(), 2);
  EXPECT_EQ(getClusterToNodeMapSize(), 1);

  auto retrieved_socket1 = socket_manager_->getConnectionSocket(node1);
  EXPECT_NE(retrieved_socket1, nullptr);

  EXPECT_EQ(getNodeToClusterMapping(node1), "");
  EXPECT_EQ(getNodeToClusterMapping(node2), cluster_id);
  cluster_nodes = getClusterToNodeMapping(cluster_id);
  EXPECT_EQ(cluster_nodes.size(), 1);
  EXPECT_EQ(cluster_nodes[0], node2);
  EXPECT_EQ(getClusterToNodeMapSize(), 1);

  // Pop the first socket for node2. Since there's still 1 socket left,.
  // cleanStaleNodeEntry should not be called yet.
  auto retrieved_socket2 = socket_manager_->getConnectionSocket(node2);
  EXPECT_NE(retrieved_socket2, nullptr);

  // Verify that node2 mappings are still present.
  EXPECT_EQ(getNodeToClusterMapping(node2), cluster_id);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node2), 1);
  cluster_nodes = getClusterToNodeMapping(cluster_id);
  EXPECT_EQ(cluster_nodes.size(), 1);
  EXPECT_EQ(cluster_nodes[0], node2);
  EXPECT_EQ(getClusterToNodeMapSize(), 1);

  // Now pop the last socket for node2. This should trigger cleanStaleNodeEntry.
  // and remove all cluster/node mappings.
  auto retrieved_socket3 = socket_manager_->getConnectionSocket(node2);
  EXPECT_NE(retrieved_socket3, nullptr);

  // Verify that node2 mappings have been cleaned up now.
  EXPECT_EQ(getNodeToClusterMapping(node1), "");
  EXPECT_EQ(getNodeToClusterMapping(node2), "");
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node2), 0);
  cluster_nodes = getClusterToNodeMapping(cluster_id);
  EXPECT_EQ(cluster_nodes.size(), 0);
  EXPECT_EQ(getClusterToNodeMapSize(), 0);
}

TEST_F(TestUpstreamSocketManager, FileEventAndTimerCleanup) {
  auto socket1 = createMockSocket(123);
  auto socket2 = createMockSocket(456);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket1), ping_interval);
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket2), ping_interval);

  EXPECT_EQ(getFDToEventMapSize(), 2);
  EXPECT_EQ(getFDToTimerMapSize(), 2);

  auto retrieved_socket1 = socket_manager_->getConnectionSocket(node_id);
  EXPECT_NE(retrieved_socket1, nullptr);

  EXPECT_FALSE(verifyFDToEventMap(123));
  EXPECT_FALSE(verifyFDToTimerMap(123));

  auto retrieved_socket2 = socket_manager_->getConnectionSocket(node_id);
  EXPECT_NE(retrieved_socket2, nullptr);

  EXPECT_EQ(getFDToEventMapSize(), 0);
  EXPECT_EQ(getFDToTimerMapSize(), 0);
}

TEST_F(TestUpstreamSocketManager, MarkSocketNotPresentDead) {
  socket_manager_->markSocketDead(999);
  socket_manager_->markSocketDead(-1);
  socket_manager_->markSocketDead(0);
}

TEST_F(TestUpstreamSocketManager, MarkIdleSocketDead) {
  auto socket1 = createMockSocket(123);
  auto socket2 = createMockSocket(456);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket1), ping_interval);
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket2), ping_interval);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 2);
  EXPECT_TRUE(verifyFDToNodeMap(123));
  EXPECT_TRUE(verifyFDToClusterMap(123));

  socket_manager_->markSocketDead(123);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 1);
  EXPECT_FALSE(verifyFDToNodeMap(123));
  EXPECT_FALSE(verifyFDToClusterMap(123));
  EXPECT_FALSE(verifyFDToEventMap(123));
  EXPECT_FALSE(verifyFDToTimerMap(123));

  auto retrieved_socket = socket_manager_->getConnectionSocket(node_id);
  EXPECT_NE(retrieved_socket, nullptr);
}

TEST_F(TestUpstreamSocketManager, MarkUsedSocketDead) {
  auto socket = createMockSocket(123);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 1);
  EXPECT_TRUE(verifyFDToNodeMap(123));
  EXPECT_TRUE(verifyFDToClusterMap(123));

  auto retrieved_socket = socket_manager_->getConnectionSocket(node_id);
  EXPECT_NE(retrieved_socket, nullptr);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 0);
  // After getConnectionSocket, fd mappings should still exist for the used socket.
  EXPECT_TRUE(verifyFDToNodeMap(123));
  EXPECT_TRUE(verifyFDToClusterMap(123));

  socket_manager_->markSocketDead(123);

  // Verify cleanStaleNodeEntry was called by checking all mappings are cleaned up.
  EXPECT_FALSE(verifyFDToNodeMap(123));
  EXPECT_FALSE(verifyFDToClusterMap(123));
  EXPECT_EQ(getNodeToClusterMapping(node_id), "");
  auto cluster_nodes = getClusterToNodeMapping(cluster_id);
  EXPECT_EQ(cluster_nodes.size(), 0);
  EXPECT_EQ(getAcceptedReverseConnectionsSize(), 0);
}

TEST_F(TestUpstreamSocketManager, MarkSocketDeadTriggerCleanup) {
  auto socket = createMockSocket(123);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 1);
  EXPECT_EQ(getNodeToClusterMapping(node_id), cluster_id);
  auto cluster_nodes = getClusterToNodeMapping(cluster_id);
  EXPECT_EQ(cluster_nodes.size(), 1);

  socket_manager_->markSocketDead(123);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 0);
  EXPECT_EQ(getNodeToClusterMapping(node_id), "");
  cluster_nodes = getClusterToNodeMapping(cluster_id);
  EXPECT_EQ(cluster_nodes.size(), 0);
  EXPECT_EQ(getAcceptedReverseConnectionsSize(), 0);
}

TEST_F(TestUpstreamSocketManager, MarkSocketDeadMultipleSockets) {
  auto socket1 = createMockSocket(123);
  auto socket2 = createMockSocket(456);
  auto socket3 = createMockSocket(789);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket1), ping_interval);
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket2), ping_interval);
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket3), ping_interval,
                                       false);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 3);
  EXPECT_EQ(getFDToEventMapSize(), 3);
  EXPECT_EQ(getFDToTimerMapSize(), 3);

  socket_manager_->markSocketDead(123);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 2);
  EXPECT_EQ(getFDToEventMapSize(), 2);
  EXPECT_EQ(getFDToTimerMapSize(), 2);
  EXPECT_FALSE(verifyFDToNodeMap(123));
  EXPECT_FALSE(verifyFDToClusterMap(123));
  EXPECT_FALSE(verifyFDToEventMap(123));
  EXPECT_FALSE(verifyFDToTimerMap(123));
  EXPECT_TRUE(verifyFDToNodeMap(456));
  EXPECT_TRUE(verifyFDToClusterMap(456));
  EXPECT_TRUE(verifyFDToNodeMap(789));
  EXPECT_TRUE(verifyFDToClusterMap(789));

  EXPECT_EQ(getNodeToClusterMapping(node_id), cluster_id);

  socket_manager_->markSocketDead(456);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 1);
  EXPECT_FALSE(verifyFDToNodeMap(456));
  EXPECT_FALSE(verifyFDToClusterMap(456));
  EXPECT_FALSE(verifyFDToEventMap(456));
  EXPECT_FALSE(verifyFDToTimerMap(456));
  EXPECT_TRUE(verifyFDToNodeMap(789));
  EXPECT_TRUE(verifyFDToClusterMap(789));

  socket_manager_->markSocketDead(789);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 0);
  EXPECT_FALSE(verifyFDToNodeMap(789));
  EXPECT_FALSE(verifyFDToClusterMap(789));
  EXPECT_EQ(getNodeToClusterMapping(node_id), "");
  EXPECT_EQ(getAcceptedReverseConnectionsSize(), 0);
  EXPECT_EQ(getFDToEventMapSize(), 0);
  EXPECT_EQ(getFDToTimerMapSize(), 0);
}

TEST_F(TestUpstreamSocketManager, PingConnectionsWriteSuccess) {
  auto socket1 = createMockSocket(123);
  auto socket2 = createMockSocket(456);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket1), ping_interval);
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket2), ping_interval);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 2);

  auto& sockets = getSocketsForNode(node_id);
  auto* mock_io_handle1 =
      dynamic_cast<NiceMock<Network::MockIoHandle>*>(&sockets.front()->ioHandle());
  auto* mock_io_handle2 =
      dynamic_cast<NiceMock<Network::MockIoHandle>*>(&sockets.back()->ioHandle());

  EXPECT_CALL(*mock_io_handle1, write(_))
      .WillRepeatedly(Invoke([](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        buffer.drain(buffer.length());
        return Api::IoCallUint64Result{0, Network::IoSocketError::getIoSocketEagainError()};
      }));
  EXPECT_CALL(*mock_io_handle2, write(_))
      .WillRepeatedly(Invoke([](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        buffer.drain(buffer.length());
        return Api::IoCallUint64Result{0, Network::IoSocketError::getIoSocketEagainError()};
      }));

  socket_manager_->pingConnections();

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 2);
}

TEST_F(TestUpstreamSocketManager, PingConnectionsWriteFailure) {
  auto socket1 = createMockSocket(123);
  auto socket2 = createMockSocket(456);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket1), ping_interval);
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket2), ping_interval);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 2);

  auto& sockets = getSocketsForNode(node_id);
  auto* mock_io_handle1 =
      dynamic_cast<NiceMock<Network::MockIoHandle>*>(&sockets.front()->ioHandle());
  auto* mock_io_handle2 =
      dynamic_cast<NiceMock<Network::MockIoHandle>*>(&sockets.back()->ioHandle());

  EXPECT_CALL(*mock_io_handle1, write(_))
      .Times(1)
      .WillRepeatedly(Invoke([](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        buffer.drain(buffer.length());
        return Api::IoCallUint64Result{0, Network::IoSocketError::create(ECONNRESET)};
      }));

  socket_manager_->pingConnections(node_id);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 1);
  EXPECT_FALSE(verifyFDToNodeMap(123));
  EXPECT_FALSE(verifyFDToClusterMap(123));
  EXPECT_TRUE(verifyFDToNodeMap(456));
  EXPECT_TRUE(verifyFDToClusterMap(456));
  EXPECT_EQ(getNodeToClusterMapping(node_id), cluster_id);
  EXPECT_EQ(getAcceptedReverseConnectionsSize(), 1);

  EXPECT_CALL(*mock_io_handle2, write(_))
      .Times(1)
      .WillRepeatedly(Invoke([](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        buffer.drain(buffer.length());
        return Api::IoCallUint64Result{0, Network::IoSocketError::create(EPIPE)};
      }));

  socket_manager_->pingConnections(node_id);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 0);
  EXPECT_FALSE(verifyFDToNodeMap(123));
  EXPECT_FALSE(verifyFDToClusterMap(123));
  EXPECT_FALSE(verifyFDToNodeMap(456));
  EXPECT_FALSE(verifyFDToClusterMap(456));
  EXPECT_EQ(getNodeToClusterMapping(node_id), "");
  EXPECT_EQ(getAcceptedReverseConnectionsSize(), 0);
}

TEST_F(TestUpstreamSocketManager, OnPingResponseValidResponse) {
  auto socket = createMockSocket(123);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval);

  auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
  EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(Return(123));

  const std::string ping_response = "RPING";
  EXPECT_CALL(*mock_io_handle, read(_, _))
      .WillOnce([&](Buffer::Instance& buffer, absl::optional<uint64_t>) -> Api::IoCallUint64Result {
        buffer.add(ping_response);
        return Api::IoCallUint64Result{ping_response.size(), Api::IoError::none()};
      });

  socket_manager_->onPingResponse(*mock_io_handle);

  EXPECT_TRUE(verifyFDToNodeMap(123));
  EXPECT_TRUE(verifyFDToClusterMap(123));
}

TEST_F(TestUpstreamSocketManager, OnPingResponseReadError) {
  auto socket = createMockSocket(123);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval);

  auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
  EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(Return(123));

  EXPECT_CALL(*mock_io_handle, read(_, _))
      .WillOnce(
          Return(Api::IoCallUint64Result{0, Network::IoSocketError::getIoSocketEagainError()}));

  socket_manager_->onPingResponse(*mock_io_handle);

  EXPECT_FALSE(verifyFDToNodeMap(123));
  EXPECT_FALSE(verifyFDToClusterMap(123));
}

TEST_F(TestUpstreamSocketManager, OnPingResponseConnectionClosed) {
  auto socket = createMockSocket(123);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval);

  auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
  EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(Return(123));

  EXPECT_CALL(*mock_io_handle, read(_, _))
      .WillOnce(Return(Api::IoCallUint64Result{0, Api::IoError::none()}));

  socket_manager_->onPingResponse(*mock_io_handle);

  EXPECT_FALSE(verifyFDToNodeMap(123));
  EXPECT_FALSE(verifyFDToClusterMap(123));
}

TEST_F(TestUpstreamSocketManager, OnPingResponseInvalidData) {
  auto socket = createMockSocket(123);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval);

  auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
  EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(Return(123));

  const std::string invalid_response = "INVALID_DATA";
  EXPECT_CALL(*mock_io_handle, read(_, _))
      .WillOnce([&](Buffer::Instance& buffer, absl::optional<uint64_t>) -> Api::IoCallUint64Result {
        buffer.add(invalid_response);
        return Api::IoCallUint64Result{invalid_response.size(), Api::IoError::none()};
      });

  // First invalid response should increment miss count but not immediately remove the fd.
  socket_manager_->onPingResponse(*mock_io_handle);
  EXPECT_TRUE(verifyFDToNodeMap(123));
  EXPECT_TRUE(verifyFDToClusterMap(123));

  // Simulate two more timeouts to cross the default threshold (3).
  socket_manager_->onPingTimeout(123);
  socket_manager_->onPingTimeout(123);
  EXPECT_FALSE(verifyFDToNodeMap(123));
  EXPECT_FALSE(verifyFDToClusterMap(123));
}

TEST_F(TestUpstreamSocketManager, GetConnectionSocketNoSocketsButValidMapping) {
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";

  addNodeToClusterMapping(node_id, cluster_id);

  auto socket = socket_manager_->getConnectionSocket(node_id);
  EXPECT_EQ(socket, nullptr);
}

TEST_F(TestUpstreamSocketManager, MarkSocketDeadInvalidSocketNotInPool) {
  auto socket = createMockSocket(123);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval);

  auto retrieved_socket = socket_manager_->getConnectionSocket(node_id);
  EXPECT_NE(retrieved_socket, nullptr);

  addFDToNodeMapping(123, node_id);

  // fd_to_cluster_map_ was erased during markSocketDead when socket
  // was retrieved, so markSocketDead should fall back to node_to_cluster_map_.
  socket_manager_->markSocketDead(123);

  EXPECT_FALSE(verifyFDToNodeMap(123));
  EXPECT_FALSE(verifyFDToClusterMap(123));

  // Test inconsistent state where fd is in fd_to_node_map_ but not in fd_to_cluster_map_.
  const int fd_456 = 456;
  addFDToNodeMapping(fd_456, node_id);
  addNodeToClusterMapping(node_id, cluster_id);

  // Mark socket dead without having fd_to_cluster_map_ entry.
  // This should log a warning, use node_to_cluster_map_ as fallback, and continue cleanup.
  socket_manager_->markSocketDead(fd_456);

  // Verify fd was removed from fd_to_node_map_ despite cluster map being missing initially.
  EXPECT_FALSE(verifyFDToNodeMap(fd_456));
  EXPECT_FALSE(verifyFDToClusterMap(fd_456));
}

TEST_F(TestUpstreamSocketManager, MarkSocketDeadClusterFallbackLogic) {
  auto socket1 = createMockSocket(123);
  auto socket2 = createMockSocket(456);
  auto socket3 = createMockSocket(789);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  // Add three sockets to test both paths (preferred and fallback).
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket1), ping_interval);
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket2), ping_interval);
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket3), ping_interval);

  EXPECT_TRUE(verifyFDToNodeMap(123));
  EXPECT_TRUE(verifyFDToClusterMap(123));
  EXPECT_TRUE(verifyFDToNodeMap(456));
  EXPECT_TRUE(verifyFDToClusterMap(456));
  EXPECT_TRUE(verifyFDToNodeMap(789));
  EXPECT_TRUE(verifyFDToClusterMap(789));

  // Retrieve first socket. This removes it from idle pool but keeps fd mappings.
  auto retrieved_socket = socket_manager_->getConnectionSocket(node_id);
  EXPECT_NE(retrieved_socket, nullptr);

  // Verify fd mappings still exist for the used socket.
  EXPECT_TRUE(verifyFDToNodeMap(123));
  EXPECT_TRUE(verifyFDToClusterMap(123));

  // Mark one idle socket dead. This should use fd_to_cluster_map_.
  // Node-to-cluster mapping should still exist since socket 789 is still in the pool.
  socket_manager_->markSocketDead(456);
  EXPECT_FALSE(verifyFDToNodeMap(456));
  EXPECT_FALSE(verifyFDToClusterMap(456));
  EXPECT_EQ(getNodeToClusterMapping(node_id), cluster_id);

  // Mark the last idle socket dead. This will trigger cleanStaleNodeEntry.
  // since the idle pool becomes empty.
  socket_manager_->markSocketDead(789);
  EXPECT_FALSE(verifyFDToNodeMap(789));
  EXPECT_FALSE(verifyFDToClusterMap(789));

  // Node-to-cluster mapping is now cleared because idle pool is empty.
  EXPECT_EQ(getNodeToClusterMapping(node_id), "");

  // Mark the used socket dead. The fd_to_cluster_map_[123] should be found and used.
  // This tests the primary path where fd_to_cluster_map_ is preferred.
  socket_manager_->markSocketDead(123);
  EXPECT_FALSE(verifyFDToNodeMap(123));
  EXPECT_FALSE(verifyFDToClusterMap(123));
}

// Socket Rebalancing Tests.

// Separate fixture because rebalancing requires 3 socket managers with named dispatchers (set in
// constructor initialization list). Merging would pollute static socket_managers_ state for all
// tests.
class TestUpstreamSocketManagerRebalancing : public testing::Test {
protected:
  TestUpstreamSocketManagerRebalancing()
      : dispatcher1_("worker_0"), dispatcher2_("worker_1"), dispatcher3_("worker_2") {
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

    // Set up default expectations for all dispatchers.
    setupDispatcher(dispatcher1_);
    setupDispatcher(dispatcher2_);
    setupDispatcher(dispatcher3_);

    // Create 3 socket managers (one per worker thread).
    socket_manager1_ = std::make_unique<UpstreamSocketManager>(dispatcher1_, extension_.get());
    socket_manager2_ = std::make_unique<UpstreamSocketManager>(dispatcher2_, extension_.get());
    socket_manager3_ = std::make_unique<UpstreamSocketManager>(dispatcher3_, extension_.get());
  }

  void setupDispatcher(NiceMock<Event::MockDispatcher>& dispatcher) {
    EXPECT_CALL(dispatcher, createTimer_(_))
        .WillRepeatedly(testing::ReturnNew<NiceMock<Event::MockTimer>>());
    EXPECT_CALL(dispatcher, createFileEvent_(_, _, _, _))
        .WillRepeatedly(testing::ReturnNew<NiceMock<Event::MockFileEvent>>());
  }

  void TearDown() override {
    socket_manager1_.reset();
    socket_manager2_.reset();
    socket_manager3_.reset();
    extension_.reset();
    socket_interface_.reset();
  }

  // Helper to manipulate node connection count for rebalancing tests.
  void setNodeConnCount(UpstreamSocketManager* manager, const std::string& node_id, int count) {
    manager->node_to_conn_count_map_[node_id] = count;
  }

  int getNodeConnCount(UpstreamSocketManager* manager, const std::string& node_id) {
    auto it = manager->node_to_conn_count_map_.find(node_id);
    return (it != manager->node_to_conn_count_map_.end()) ? it->second : 0;
  }

  size_t verifyAcceptedReverseConnectionsMap(UpstreamSocketManager* manager,
                                             const std::string& node_id) {
    auto it = manager->accepted_reverse_connections_.find(node_id);
    if (it == manager->accepted_reverse_connections_.end()) {
      return 0;
    }
    return it->second.size();
  }

  // Helper to create a mock socket with proper address setup.
  Network::ConnectionSocketPtr createMockSocket(int fd = 123,
                                                const std::string& local_addr = "127.0.0.1:8080",
                                                const std::string& remote_addr = "127.0.0.1:9090") {
    auto socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();

    // Parse local address (IP:port format).
    auto local_colon_pos = local_addr.find(':');
    std::string local_ip = local_addr.substr(0, local_colon_pos);
    uint32_t local_port = std::stoi(local_addr.substr(local_colon_pos + 1));
    auto local_address = Network::Utility::parseInternetAddressNoThrow(local_ip, local_port);

    // Parse remote address (IP:port format).
    auto remote_colon_pos = remote_addr.find(':');
    std::string remote_ip = remote_addr.substr(0, remote_colon_pos);
    uint32_t remote_port = std::stoi(remote_addr.substr(remote_colon_pos + 1));
    auto remote_address = Network::Utility::parseInternetAddressNoThrow(remote_ip, remote_port);

    // Create a mock IO handle and set it up.
    auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
    auto* mock_io_handle_ptr = mock_io_handle.get();
    EXPECT_CALL(*mock_io_handle_ptr, fdDoNotUse()).WillRepeatedly(Return(fd));
    EXPECT_CALL(*socket, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle_ptr));

    // Store the mock_io_handle in the socket.
    socket->io_handle_ = std::move(mock_io_handle);

    // Set up connection info provider with the desired addresses.
    socket->connection_info_provider_->setLocalAddress(local_address);
    socket->connection_info_provider_->setRemoteAddress(remote_address);

    return socket;
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<ThreadLocal::MockInstance> thread_local_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopeSharedPtr stats_scope_;

  NiceMock<Event::MockDispatcher> dispatcher1_;
  NiceMock<Event::MockDispatcher> dispatcher2_;
  NiceMock<Event::MockDispatcher> dispatcher3_;

  envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface config_;

  std::unique_ptr<ReverseTunnelAcceptor> socket_interface_;
  std::unique_ptr<ReverseTunnelAcceptorExtension> extension_;

  std::unique_ptr<UpstreamSocketManager> socket_manager1_;
  std::unique_ptr<UpstreamSocketManager> socket_manager2_;
  std::unique_ptr<UpstreamSocketManager> socket_manager3_;

  // Set log level to debug for this test class.
  LogLevelSetter log_level_setter_ = LogLevelSetter(spdlog::level::debug);
};

TEST_F(TestUpstreamSocketManagerRebalancing, RebalanceToLeastLoadedWorker) {
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  // Set up connection counts: worker1 has 5, worker2 has 0, worker3 has 3.
  setNodeConnCount(socket_manager1_.get(), node_id, 5);
  setNodeConnCount(socket_manager2_.get(), node_id, 0);
  setNodeConnCount(socket_manager3_.get(), node_id, 3);

  // Track which dispatcher's post() was called.
  bool dispatcher1_post_called = false;
  bool dispatcher2_post_called = false;
  bool dispatcher3_post_called = false;

  // Mock post() on dispatcher2 (the least loaded) to execute the lambda.
  EXPECT_CALL(dispatcher2_, post(_)).WillOnce(Invoke([&](Event::PostCb callback) {
    dispatcher2_post_called = true;
    callback(); // Execute the lambda immediately.
  }));

  // Mock post() on other dispatchers (should not be called).
  EXPECT_CALL(dispatcher1_, post(_)).WillRepeatedly(Invoke([&](Event::PostCb callback) {
    dispatcher1_post_called = true;
    callback();
  }));

  EXPECT_CALL(dispatcher3_, post(_)).WillRepeatedly(Invoke([&](Event::PostCb callback) {
    dispatcher3_post_called = true;
    callback();
  }));

  // Create socket and add it to worker1 (with rebalanced = false to trigger rebalancing).
  auto socket = createMockSocket(123);
  socket_manager1_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval,
                                        false /* rebalanced */);

  // Verify that post() was called on dispatcher2 (least loaded).
  EXPECT_FALSE(dispatcher1_post_called);
  EXPECT_TRUE(dispatcher2_post_called);
  EXPECT_FALSE(dispatcher3_post_called);

  // Verify socket was added to socket_manager2, not socket_manager1.
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(socket_manager1_.get(), node_id), 0);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(socket_manager2_.get(), node_id), 1);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(socket_manager3_.get(), node_id), 0);

  // Verify connection count was incremented on the target worker.
  EXPECT_EQ(getNodeConnCount(socket_manager1_.get(), node_id), 5);
  EXPECT_EQ(getNodeConnCount(socket_manager2_.get(), node_id), 1); // Incremented from 0 to 1.
  EXPECT_EQ(getNodeConnCount(socket_manager3_.get(), node_id), 3);
}

TEST_F(TestUpstreamSocketManagerRebalancing, NoRebalancingWhenCurrentWorkerIsLeastLoaded) {
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  // Set up connection counts: worker1 has 0 (least loaded), worker2 has 3, worker3 has 5.
  setNodeConnCount(socket_manager1_.get(), node_id, 0);
  setNodeConnCount(socket_manager2_.get(), node_id, 3);
  setNodeConnCount(socket_manager3_.get(), node_id, 5);

  // Mock post() on all dispatchers (should not be called since no rebalancing needed).
  EXPECT_CALL(dispatcher1_, post(_)).Times(0);

  EXPECT_CALL(dispatcher2_, post(_)).Times(0);

  EXPECT_CALL(dispatcher3_, post(_)).Times(0);

  // Create socket and add it to worker1 (with rebalanced = false to trigger check).
  auto socket = createMockSocket(123);
  socket_manager1_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval,
                                        false /* rebalanced */);

  // Verify socket was added to socket_manager1 directly.
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(socket_manager1_.get(), node_id), 1);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(socket_manager2_.get(), node_id), 0);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(socket_manager3_.get(), node_id), 0);

  // Verify connection count was incremented on worker1.
  EXPECT_EQ(getNodeConnCount(socket_manager1_.get(), node_id), 1); // Incremented from 0 to 1.
  EXPECT_EQ(getNodeConnCount(socket_manager2_.get(), node_id), 3);
  EXPECT_EQ(getNodeConnCount(socket_manager3_.get(), node_id), 5);
}

TEST_F(TestUpstreamSocketManagerRebalancing, RebalancingWithNewNode) {
  const std::string node_id = "new-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  // Set up connection counts for different node: worker1 has 10, worker2 has 5, worker3 has 8.
  setNodeConnCount(socket_manager1_.get(), "other-node", 10);
  setNodeConnCount(socket_manager2_.get(), "other-node", 5);
  setNodeConnCount(socket_manager3_.get(), "other-node", 8);

  // New node has no entries, so all workers have count 0 for it.
  // Worker1 should be selected as it's the first one with count 0.

  // Mock post() should not be called since worker1 is calling addConnectionSocket.
  EXPECT_CALL(dispatcher1_, post(_)).Times(0);
  EXPECT_CALL(dispatcher2_, post(_)).Times(0);
  EXPECT_CALL(dispatcher3_, post(_)).Times(0);

  // Create socket and add it to worker1 (with rebalanced = false).
  auto socket = createMockSocket(789);
  socket_manager1_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval,
                                        false /* rebalanced */);

  // Verify socket was added to socket_manager1 directly (it's the least loaded for this node).
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(socket_manager1_.get(), node_id), 1);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(socket_manager2_.get(), node_id), 0);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(socket_manager3_.get(), node_id), 0);

  // Verify connection count was incremented on worker1 for the new node.
  EXPECT_EQ(getNodeConnCount(socket_manager1_.get(), node_id), 1);
  EXPECT_EQ(getNodeConnCount(socket_manager2_.get(), node_id), 0);
  EXPECT_EQ(getNodeConnCount(socket_manager3_.get(), node_id), 0);
}

TEST_F(TestUpstreamSocketManagerRebalancing, RebalancingSkippedWhenAlreadyRebalanced) {
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  // Set up connection counts: worker3 has 10, worker1 and worker2 have 0.
  setNodeConnCount(socket_manager1_.get(), node_id, 0);
  setNodeConnCount(socket_manager2_.get(), node_id, 0);
  setNodeConnCount(socket_manager3_.get(), node_id, 10);

  // Mock post() should not be called since rebalanced = true.
  EXPECT_CALL(dispatcher1_, post(_)).Times(0);
  EXPECT_CALL(dispatcher2_, post(_)).Times(0);
  EXPECT_CALL(dispatcher3_, post(_)).Times(0);

  // Create socket and add it to worker3 with rebalanced = true (skip rebalancing).
  auto socket = createMockSocket(999);
  socket_manager3_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval,
                                        true /* rebalanced */);

  // Verify socket was added to socket_manager3 directly (rebalancing was skipped).
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(socket_manager1_.get(), node_id), 0);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(socket_manager2_.get(), node_id), 0);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(socket_manager3_.get(), node_id), 1);

  // Verify connection count. This should not be incremented by pickLeastLoadedSocketManager.
  EXPECT_EQ(getNodeConnCount(socket_manager1_.get(), node_id), 0);
  EXPECT_EQ(getNodeConnCount(socket_manager2_.get(), node_id), 0);
  EXPECT_EQ(getNodeConnCount(socket_manager3_.get(), node_id), 10);
}

} // namespace ReverseConnection.
} // namespace Bootstrap.
} // namespace Extensions.
} // namespace Envoy.
